/* Exercise the real grid and count array reads only in its implementation.
 * A normal one-cell move must not inspect every cell of a large room. */
#include "../../runner.h"
#include "../../collision.h"
#include "../../spatial_grid.h"
#include "../../utils.h"
#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned arrayReads;
static ptrdiff_t observedLength(const void* array) {
    ++arrayReads;
    return stbds_arrlen(array);
}
#undef arrlen
#define arrlen(a) observedLength(a)
#include "../../spatial_grid.c"
#undef arrlen
#define arrlen stbds_arrlen

typedef struct {
    Runner runner;
    DataWin data;
    Sprite sprite;
    Instance* inst;
} Fixture;
static unsigned checks, workFailures;
static bool behaviorOnly;

static Fixture* fixture(int size) {
    Fixture* f = (Fixture*)calloc(1, sizeof(*f));
    assert(f);
    f->runner.dataWin = &f->data;
#ifdef GAME_DELTARUNE
    f->data.gen8.wadVersion = 17;
#else
    f->data.gen8.wadVersion = 16;
#endif
    f->data.sprt.count = 1;
    f->data.sprt.sprites = &f->sprite;
    f->sprite.width = f->sprite.height = size;
    f->sprite.bboxMode = 1;
    f->runner.spatialGrid = SpatialGrid_create(4096, 4096);
    f->inst = Instance_create(100001, 0, 10, 10);
    f->inst->spriteIndex = 0;
    f->inst->maskIndex = -1;
    hmput(f->runner.instancesById, f->inst->instanceId, f->inst);
    return f;
}
static void syncInstance(Fixture* f, int x, int y) {
    f->inst->x = (float)x;
    f->inst->y = (float)y;
    SpatialGrid_markInstanceAsDirty(f->runner.spatialGrid, f->inst);
    SpatialGrid_syncGrid(&f->runner, f->runner.spatialGrid);
}
static unsigned references(SpatialGrid* grid, Instance* inst) {
    unsigned count = 0;
    int cells = grid->gridWidth * grid->gridHeight;
    for (int i = 0; i < cells; ++i) {
        Instance** cell = grid->grid[i];
        for (int j = 0; j < arrlen(cell); ++j) {
            if (cell[j] == inst) ++count;
        }
    }
    return count;
}
static void dispose(Fixture* f) {
    SpatialGrid_removeInstance(f->runner.spatialGrid, f->inst);
    assert(references(f->runner.spatialGrid, f->inst) == 0);
    Instance_free(f->inst);
    SpatialGrid_free(f->runner.spatialGrid);
    hmfree(f->runner.instancesById);
    free(f);
}
static void expectMoved(Fixture* f, int x, int y, unsigned count) {
    syncInstance(f, x, y);
    assert(references(f->runner.spatialGrid, f->inst) == count);
    assert(arrlen(f->runner.spatialGrid->grid[0]) == 0);
    assert(!f->inst->spatialGridDirty);
    ++checks;
}
static void testNormalMove(void) {
    Fixture* f = fixture(16);
    syncInstance(f, 10, 10);
    assert(references(f->runner.spatialGrid, f->inst) == 1);
    arrayReads = 0;
    syncInstance(f, 130, 130);
    unsigned reads = arrayReads;
    printf("one-cell move: %u array reads\n", reads);
    assert(references(f->runner.spatialGrid, f->inst) == 1);
    assert(f->runner.spatialGrid->grid[SpatialGrid_cellIndex(f->runner.spatialGrid, 2, 2)][0] == f->inst);
    if (!behaviorOnly && reads >= 64) {
        fprintf(stderr, "one-cell move: %u array reads; expected fewer than 64\n", reads);
        ++workFailures;
    }
    ++checks;
    dispose(f);
}
static void testLostAndPartialTracking(void) {
    Fixture* lost = fixture(16);
    syncInstance(lost, 10, 10);
    arrsetlen(lost->inst->collisionCells, 0);
    expectMoved(lost, 260, 260, 1);
    dispose(lost);
    Fixture* partial = fixture(100);
    syncInstance(partial, 10, 10);
    assert(arrlen(partial->inst->collisionCells) == 4);
    arrsetlen(partial->inst->collisionCells, 1);
    expectMoved(partial, 260, 260, 4);
    dispose(partial);
}
static void testInvalidAndRepeatedCoordinates(void) {
    Fixture* invalid = fixture(16);
    syncInstance(invalid, 10, 10);
    invalid->inst->collisionCells[0] = (int32_t)SpatialGrid_packGridCoordinates(65535, 65535);
    expectMoved(invalid, 260, 260, 1);
    dispose(invalid);
    Fixture* repeated = fixture(100);
    syncInstance(repeated, 10, 10);
    repeated->inst->collisionCells[1] = repeated->inst->collisionCells[0];
    expectMoved(repeated, 260, 260, 4);
    dispose(repeated);
}
static void testDuplicateRecovery(void) {
    Fixture* f = fixture(16);
    syncInstance(f, 10, 10);
    arrput(f->runner.spatialGrid->grid[0], f->inst);
    int last = f->runner.spatialGrid->gridWidth * f->runner.spatialGrid->gridHeight - 1;
    arrput(f->runner.spatialGrid->grid[last], f->inst);
    expectMoved(f, 260, 260, 1);
    dispose(f);
}
static void testPublicRecoveryAndStableOrder(void) {
    Fixture* f = fixture(16);
    Instance a = {0}, b = {0};
    syncInstance(f, 10, 10);
    arrput(f->runner.spatialGrid->grid[0], &a);
    arrput(f->runner.spatialGrid->grid[0], f->inst);
    arrput(f->runner.spatialGrid->grid[0], &b);
    int last = f->runner.spatialGrid->gridWidth * f->runner.spatialGrid->gridHeight - 1;
    arrput(f->runner.spatialGrid->grid[last], f->inst);
    arrsetlen(f->inst->collisionCells, 0);
    SpatialGrid_removeInstance(f->runner.spatialGrid, f->inst);
    assert(references(f->runner.spatialGrid, f->inst) == 0);
    assert(arrlen(f->runner.spatialGrid->grid[0]) == 2);
    assert(f->runner.spatialGrid->grid[0][0] == &a && f->runner.spatialGrid->grid[0][1] == &b);
    ++checks;
    dispose(f);
}
static void testTrustedRemovalStableOrder(void) {
    Fixture* f = fixture(100);
    Instance a = {0}, b = {0};
    syncInstance(f, 10, 10);
    for (int i = 0; i < arrlen(f->inst->collisionCells); ++i) {
        uint32_t coordinate = f->inst->collisionCells[i];
        int index = SpatialGrid_cellIndex(f->runner.spatialGrid,
            SpatialGrid_unpackGridX(coordinate), SpatialGrid_unpackGridY(coordinate));
        Instance** cell = f->runner.spatialGrid->grid[index];
        arrput(cell, &a);
        arrput(cell, &b);
        f->runner.spatialGrid->grid[index] = cell;
    }
    arrayReads = 0;
    syncInstance(f, 260, 260);
    assert(arrayReads < 64);
    assert(references(f->runner.spatialGrid, f->inst) == 4);
    int cells = f->runner.spatialGrid->gridWidth * f->runner.spatialGrid->gridHeight;
    for (int i = 0; i < cells; ++i) {
        Instance** cell = f->runner.spatialGrid->grid[i];
        if (arrlen(cell) == 2)
            assert(cell[0] == &a && cell[1] == &b);
    }
    ++checks;
    dispose(f);
}
static void testNoMatchPreservesLegacyState(void) {
    Fixture* f = fixture(16);
    arrput(f->inst->collisionCells, (int32_t)SpatialGrid_packGridCoordinates(1, 1));
    f->inst->spatialGridDirty = true;
    SpatialGrid_removeInstance(f->runner.spatialGrid, f->inst);
    assert(arrlen(f->inst->collisionCells) == 1 && f->inst->spatialGridDirty);
    ++checks;
    dispose(f);
}
static void testGridReplacement(void) {
    Fixture* f = fixture(16);
    syncInstance(f, 10, 10);
    SpatialGrid* old = f->runner.spatialGrid;
    f->runner.spatialGrid = SpatialGrid_create(4096, 4096);
    arrput(f->runner.spatialGrid->grid[0], f->inst);
    int last = f->runner.spatialGrid->gridWidth * f->runner.spatialGrid->gridHeight - 1;
    arrput(f->runner.spatialGrid->grid[last], f->inst);
    syncInstance(f, 130, 130);
    assert(references(f->runner.spatialGrid, f->inst) == 1);
    SpatialGrid_removeInstance(old, f->inst);
    assert(references(old, f->inst) == 0);
    expectMoved(f, 260, 260, 1);
    SpatialGrid_free(old);
    dispose(f);
}
static void testMissingTrackedReference(void) {
    Fixture* f = fixture(100);
    syncInstance(f, 10, 10);
    arrsetlen(f->runner.spatialGrid->grid[0], 0);
    int last = f->runner.spatialGrid->gridWidth * f->runner.spatialGrid->gridHeight - 1;
    arrput(f->runner.spatialGrid->grid[last], f->inst);
    expectMoved(f, 260, 260, 4);
    dispose(f);
}
static void testInactiveReactivation(void) {
    Fixture* f = fixture(16);
    syncInstance(f, 10, 10);
    f->inst->active = false;
    SpatialGrid_markInstanceAsDirty(f->runner.spatialGrid, f->inst);
    assert(references(f->runner.spatialGrid, f->inst) == 0);
    f->inst->active = true;
    expectMoved(f, 130, 130, 1);
    dispose(f);
}
static void testEmptyMembership(void) {
    Fixture* f = fixture(16);
    f->inst->spriteIndex = -1;
    syncInstance(f, 10, 10);
    arrayReads = 0;
    syncInstance(f, 130, 130);
    assert(arrayReads < 64 && references(f->runner.spatialGrid, f->inst) == 0);
    ++checks;
    dispose(f);
}
/* Exhaustion must disable trust permanently, including newly allocated grids. */
static void testGenerationExhaustion(void) {
    nextGridGenerationLow = UINT32_MAX;
    nextGridGenerationHigh = UINT32_MAX;
    SpatialGrid* last = SpatialGrid_create(128, 128);
    assert(last->generationLow == UINT32_MAX && last->generationHigh == UINT32_MAX);
    SpatialGrid* exhausted = SpatialGrid_tryCreate(128, 128);
    assert(exhausted);
    assert(exhausted->generationLow == 0 && exhausted->generationHigh == 0);
    Fixture* f = fixture(16);
    assert(f->runner.spatialGrid->generationLow == 0 && f->runner.spatialGrid->generationHigh == 0);
    syncInstance(f, 10, 10);
    arrayReads = 0;
    syncInstance(f, 130, 130);
    assert(arrayReads > 1000);
    assert(references(f->runner.spatialGrid, f->inst) == 1);
    ++checks;
    dispose(f);
    SpatialGrid_free(exhausted);
    SpatialGrid_free(last);
}
int main(int argc, char** argv) {
    behaviorOnly = argc == 2 && !strcmp(argv[1], "--behavior-only");
    testNormalMove();
    testLostAndPartialTracking();
    testInvalidAndRepeatedCoordinates();
    testDuplicateRecovery();
    testPublicRecoveryAndStableOrder();
    testTrustedRemovalStableOrder();
    testNoMatchPreservesLegacyState();
    testGridReplacement();
    testMissingTrackedReference();
    testInactiveReactivation();
    testEmptyMembership();
    testGenerationExhaustion();
    printf("spatial grid: %u behavior checks, %u bounded-work failures\n", checks, workFailures);
    return workFailures ? EXIT_FAILURE : EXIT_SUCCESS;
}
