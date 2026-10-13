/* ROOM chunk cache must preserve absolute WAD offsets and lazy payload semantics. */
#include "data_win.h"
#include "binary_reader.h"
#include "log.h"
#include <assert.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

void platformLog(logType type, const char *format, va_list args) {
    if (type >= LOG_TYPE_WARNING) vfprintf(stderr, format, args);
}

static DataWin *parse(const char *path, bool cache) {
    DataWinParserOptions options = {0};
    options.parseGen8 = true;
    options.parseStrg = true;
    options.parseRoom = true;
    options.lazyLoadRooms = true;
    options.loadType = DATAWINLOADTYPE_LOAD_PER_CHUNK;
    options.cacheRawRoomChunk = cache;
    return DataWin_parse(path, options);
}

static void compareRoom(const Room *a, const Room *b) {
    assert(a->payloadLoaded && b->payloadLoaded);
    assert(a->gameObjectCount == b->gameObjectCount);
    assert(a->tileCount == b->tileCount);
    assert(a->layerCount == b->layerCount);
    for (unsigned i = 0; i < 8; i++) {
        const RoomBackground *x = &a->backgrounds[i], *y = &b->backgrounds[i];
        assert(x->enabled == y->enabled && x->foreground == y->foreground);
        assert(x->backgroundDefinition == y->backgroundDefinition);
        assert(x->x == y->x && x->y == y->y);
        assert(x->tileX == y->tileX && x->tileY == y->tileY);
        assert(x->speedX == y->speedX && x->speedY == y->speedY);
        assert(x->stretch == y->stretch);
        const RoomView *v = &a->views[i], *w = &b->views[i];
        assert(v->enabled == w->enabled);
        assert(v->viewX == w->viewX && v->viewY == w->viewY);
        assert(v->viewWidth == w->viewWidth && v->viewHeight == w->viewHeight);
        assert(v->portX == w->portX && v->portY == w->portY);
        assert(v->portWidth == w->portWidth && v->portHeight == w->portHeight);
        assert(v->borderX == w->borderX && v->borderY == w->borderY);
        assert(v->speedX == w->speedX && v->speedY == w->speedY);
        assert(v->objectId == w->objectId);
    }
    for (uint32_t i = 0; i < a->gameObjectCount; i++) {
        const RoomGameObject *x = &a->gameObjects[i], *y = &b->gameObjects[i];
        assert(x->x == y->x && x->y == y->y);
        assert(x->objectDefinition == y->objectDefinition && x->instanceID == y->instanceID);
        assert(x->creationCode == y->creationCode && x->color == y->color);
        assert(memcmp(&x->scaleX, &y->scaleX, sizeof(float)) == 0);
        assert(memcmp(&x->scaleY, &y->scaleY, sizeof(float)) == 0);
        assert(memcmp(&x->imageSpeed, &y->imageSpeed, sizeof(float)) == 0);
        assert(x->imageIndex == y->imageIndex && x->preCreateCode == y->preCreateCode);
        assert(memcmp(&x->rotation, &y->rotation, sizeof(float)) == 0);
    }
    for (uint32_t i = 0; i < a->tileCount; i++) {
        const RoomTile *x = &a->tiles[i], *y = &b->tiles[i];
        assert(x->x == y->x && x->y == y->y);
        assert(x->instanceID == y->instanceID && x->backgroundDefinition == y->backgroundDefinition);
        assert(x->useSpriteDefinition == y->useSpriteDefinition);
        assert(x->sourceX == y->sourceX && x->sourceY == y->sourceY);
        assert(x->width == y->width && x->height == y->height);
        assert(x->tileDepth == y->tileDepth && x->color == y->color);
        assert(memcmp(&x->scaleX, &y->scaleX, sizeof(float)) == 0);
        assert(memcmp(&x->scaleY, &y->scaleY, sizeof(float)) == 0);
        assert(memcmp(&x->alpha, &y->alpha, sizeof(float)) == 0);
    }
}

static void requireReaderAbort(int operation, DataWin *cached) {
    int pipeFd[2];
    assert(pipe(pipeFd) == 0);
    pid_t child = fork();
    assert(child >= 0);
    if (child == 0) {
        close(pipeFd[0]);
        assert(dup2(pipeFd[1], STDERR_FILENO) == STDERR_FILENO);
        close(pipeFd[1]);
        if (operation == 2) {
            uint32_t room = 0;
            while (room < cached->room.count && !cached->room.rooms[room].present) room++;
            assert(room < cached->room.count);
            cached->room.rooms[room].backgroundsFileOffset = (uint32_t)(cached->roomChunkBase - 1);
            DataWin_loadRoomPayload(cached, (int32_t)room);
        } else {
            BinaryReader reader = BinaryReader_create(NULL, 16);
            if (operation == 1) BinaryReader_seek(&reader, 4);
            else (void)BinaryReader_readUint32(&reader);
        }
        _exit(0);
    }
    close(pipeFd[1]);
    char output[4096] = {0};
    size_t kept = 0;
    char block[512];
    ssize_t n;
    while ((n = read(pipeFd[0], block, sizeof(block))) > 0) {
        size_t copy = (size_t)n < sizeof(output) - kept - 1 ? (size_t)n : sizeof(output) - kept - 1;
        memcpy(output + kept, block, copy);
        kept += copy;
    }
    close(pipeFd[0]);
    int status = 0;
    assert(waitpid(child, &status, 0) == child);
    assert(WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT);
    assert(strstr(output, operation == 2 ? "BinaryReader: buffer seek" :
                  "BinaryReader: missing file") != NULL);
}

int main(int argc, char **argv) {
    assert(argc == 2);
    requireReaderAbort(0, NULL);
    requireReaderAbort(1, NULL);
    DataWin *disk = parse(argv[1], false);
    DataWin *cached = parse(argv[1], true);
    assert(disk && cached && disk->room.count > 0);
    assert(disk->room.count == cached->room.count);
    assert(disk->roomChunkBuffer == NULL);
    assert(cached->roomChunkBuffer && cached->roomChunkSize > 0);
    assert(cached->roomChunkBase > 8);

    /* After the startup pass, memory-backed room loading needs no file reads. */
    fclose(cached->lazyLoadFile);
    cached->lazyLoadFile = NULL;
    requireReaderAbort(2, cached);
    unsigned compared = 0, populated = 0;
    for (uint32_t i = 0; i < disk->room.count; i++) {
        if (!disk->room.rooms[i].present) continue;
        assert(!disk->room.rooms[i].payloadLoaded);
        assert(!cached->room.rooms[i].payloadLoaded);
        DataWin_loadRoomPayload(disk, (int32_t)i);
        DataWin_loadRoomPayload(cached, (int32_t)i);
        compareRoom(&disk->room.rooms[i], &cached->room.rooms[i]);
        if (disk->room.rooms[i].gameObjectCount || disk->room.rooms[i].tileCount)
            populated++;
        DataWin_freeRoomPayload(&disk->room.rooms[i]);
        DataWin_freeRoomPayload(&cached->room.rooms[i]);
        assert(!disk->room.rooms[i].payloadLoaded && !cached->room.rooms[i].payloadLoaded);
        DataWin_loadRoomPayload(disk, (int32_t)i);
        DataWin_loadRoomPayload(cached, (int32_t)i);
        compareRoom(&disk->room.rooms[i], &cached->room.rooms[i]);
        compared++;
    }
    assert(compared > 0 && populated > 0);
    printf("ROOM bytes=%zu, rooms=%u, populated=%u\n",
           cached->roomChunkSize, compared, populated);
    DataWin_free(disk);
    DataWin_free(cached); /* owns and frees retained ROOM bytes */
    puts("room cache parity and cleanup: pass");
    return 0;
}
