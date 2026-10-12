#define OF_PC 1
#include <stdio.h>
#include <unistd.h>
static int writeFault, closeFault;
static size_t testWrite(const void *p, size_t size, size_t count, FILE *f) {
    if (writeFault) { fwrite(p, size, count / 2, f); return count / 2; }
    return fwrite(p, size, count, f);
}
static int testClose(FILE *f) {
    int rc = fclose(f);
    if (closeFault) { closeFault = 0; return EOF; }
    return rc;
}
#define fwrite testWrite
#define fclose testClose
#include "../src/openfpga/platform/ut_save_fs.c"
#undef fwrite
#undef fclose
#include "../src/openfpga/platform/ut_snapshot_parts.h"
#include <assert.h>

void logWarn(const char *fmt, ...) { (void)fmt; }
void logError(const char *fmt, ...) { (void)fmt; abort(); }
void logInfo(const char *fmt, ...) { (void)fmt; }

int main(void) {
    UtSaveFs fs = {0};
    const char *reason = NULL;
    uint8_t *bytes = NULL;
    size_t size = 0;
    g_volatile = true;
    assert(putFile(&fs, "file0", (const uint8_t *)"before", 6));
    assert(utSaveFsSnapshot(&fs.base, &bytes, &size, &reason));
    assert(putFile(&fs, "file0", (const uint8_t *)"after", 5));
    assert(putFile(&fs, "newfile", (const uint8_t *)"new", 3));
    void *p = utSaveFsPrepareSnapshot(&fs.base, bytes, size, &reason);
    assert(p && fs.fileCount == 2); /* prepare must not change the game */
    assert(utSaveFsApplySnapshot(&fs.base, p));
    utSaveFsFreeSnapshot(p);
    assert(fs.fileCount == 1 && findFile(&fs, "newfile") == NULL);
    assert(findFile(&fs, "file0")->size == 6);
    assert(memcmp(findFile(&fs, "file0")->data, "before", 6) == 0);
    char directory[] = "/tmp/ut-archive-XXXXXX";
    char original[4096]; assert(getcwd(original, sizeof(original)));
    assert(mkdtemp(directory) && chdir(directory) == 0);
    g_volatile = false;
    assert(putFile(&fs, "file0", (const uint8_t *)"after", 5));
    uint8_t persisted[512], observed[512];
    FILE *slot = fopen(UT_SAVE_SLOT_FILE, "rb"); assert(slot);
    size_t persistedSize = fread(persisted, 1, sizeof(persisted), slot); fclose(slot);
    for (int fault = 0; fault < 2; fault++) {
        p = utSaveFsPrepareSnapshot(&fs.base, bytes, size, &reason); assert(p);
        writeFault = fault == 0; closeFault = fault == 1;
        assert(!utSaveFsApplySnapshot(&fs.base, p));
        writeFault = closeFault = 0; utSaveFsFreeSnapshot(p);
        slot = fopen(UT_SAVE_SLOT_FILE, "rb"); assert(slot);
        size_t observedSize = fread(observed, 1, sizeof(observed), slot); fclose(slot);
        assert(observedSize == persistedSize && !memcmp(persisted, observed, persistedSize));
        assert(findFile(&fs, "file0")->size == 5);
    }
    p = utSaveFsPrepareSnapshot(&fs.base, bytes, size, &reason); assert(p);
    assert(utSaveFsApplySnapshot(&fs.base, p)); utSaveFsFreeSnapshot(p);
    assert(findFile(&fs, "file0")->size == 6);
    remove(UT_SAVE_SLOT_FILE); assert(chdir(original) == 0); rmdir(directory);
    g_volatile = true;
    for (size_t n = 0; n < size; n++) {
        p = utSaveFsPrepareSnapshot(&fs.base, bytes, n, &reason);
        assert(p == NULL && fs.fileCount == 1);
    }
    bytes[16] = 0; /* invalid empty archive entry name */
    assert(utSaveFsPrepareSnapshot(&fs.base, bytes, size, &reason) == NULL);
    free(bytes);
    free(fs.files[0].data);
    puts("progress archive snapshot: passed");
    return 0;
}
