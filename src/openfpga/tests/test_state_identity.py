"""Exercise current/legacy game identity selection with real snapshot envelopes."""
from pathlib import Path
import re
import subprocess
import tempfile

repo = Path(__file__).resolve().parents[3]
source = (repo / 'src/openfpga/platform/of_state_requests.c').read_text()


def function(name):
    match = re.search(r'(?:static bool|void) ' + name + r'\(', source)
    start = source.index('{', match.start())
    end, depth = start + 1, 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[match.start():end]


pre = r'''
#include "of_snapshot_envelope.h"
#include "of_snapshot_bytes.h"
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#define BS_FNV64_OFFSET UINT64_C(14695981039346656037)
#define BS_FNV64_PRIME UINT64_C(1099511628211)
typedef struct { bool present; char *name; size_t bytecodeAbsoluteOffset, length; } CodeEntry;
typedef struct {
    struct { uint32_t gameID; uint64_t timestamp; uint8_t licenseMD5[16]; } gen8;
    struct { uint32_t count; CodeEntry *entries; } code;
    uint8_t *bytecodeBuffer; size_t bytecodeBufferBase;
} DataWin;
typedef struct { DataWin *dataWin; void *audioSystem, *fileSystem; int state; } Runner;
typedef struct { int unused; } RunnerSnapshot;
static RunnerSnapshot core;
static int audio, files, prepared, locked;
static uint64_t preparedId;
static uint64_t nowNanos(void) { static uint64_t n; return ++n; }
static void logInfo(const char *s, ...) { (void)s; }
static void ofAudioSnapshotLock(bool b) { locked = b; }
static RunnerSnapshot *Runner_snapshotPrepare(Runner *r, uint64_t id,
        const uint8_t *d, size_t n, const char **reason) {
    (void)r; prepared++; preparedId = id;
    OfSnapshotReader reader = {d, n, 0, true};
    uint64_t inner = ofSnapGetU32(&reader); inner |= (uint64_t)ofSnapGetU32(&reader) << 32;
    if (!reader.ok || inner != id) { *reason = "inner identity"; return NULL; }
    return &core;
}
static void *ofAudioPrepareSnapshot(void *r, const uint8_t *d, size_t n, const char **s) {
    (void)r; (void)d; (void)n; (void)s; return &audio;
}
static void *ofSaveFsPrepareSnapshot(void *r, const uint8_t *d, size_t n, const char **s) {
    (void)r; (void)d; (void)n; (void)s; return &files;
}
static bool ofSaveFsApplySnapshot(void *r, void *p) { (void)r; (void)p; return true; }
static void Runner_snapshotApply(Runner *r, RunnerSnapshot *p) { (void)p; r->state++; }
static void ofAudioApplySnapshot(void *r, void *p) { (void)r; (void)p; }
static void ofPlatformSnapshotResumed(Runner *r, const uint32_t *v) { (void)r; (void)v; }
static void Runner_snapshotFree(RunnerSnapshot *p) { (void)p; }
static void ofAudioFreeSnapshot(void *p) { (void)p; }
static void ofSaveFsFreeSnapshot(void *p) { (void)p; }
'''
post = r'''
static uint64_t hash(uint64_t h, const void *data, size_t n) {
    const uint8_t *bytes = data;
    for (size_t i = 0; i < n; ++i) h = (h ^ bytes[i]) * BS_FNV64_PRIME;
    return h;
}
static void check(Runner *r, uint64_t outer, uint64_t inner, bool expected,
                  bool damaged, bool truncated, bool expectPrepared) {
    uint8_t id[8], platform[12] = {0}, content[] = {1};
    for (unsigned i = 0; i < 8; ++i) id[i] = inner >> (8 * i);
    OfSnapshotSection sections[4] = {{id, 8}, {content, 1}, {content, 1}, {platform, 12}};
    uint8_t *data; size_t n;
    assert(ofSnapshotPack(outer, sections, &data, &n));
    if (damaged) data[n - 1] ^= 1;
    prepared = r->state = locked = 0; const char *reason = NULL;
    bool ok = restoreState(r, data, n - truncated, &reason, NULL);
    assert(ok == expected); assert(r->state == expected); assert(!locked);
    assert((prepared != 0) == expectPrepared);
    if (expectPrepared) assert(preparedId == outer);
    if (!expected) assert(reason);
    free(data);
}
int main(void) {
    uint8_t code[] = {1, 2, 3, 4};
    CodeEntry entry = {true, "fixture", 100, sizeof(code)};
    DataWin dw = {.gen8 = {42, 123456, {9}}, .code = {1, &entry},
                  .bytecodeBuffer = code, .bytecodeBufferBase = 100};
    Runner r = {.dataWin = &dw};
    uint64_t legacy = BS_FNV64_OFFSET;
    legacy = hash(legacy, &dw.gen8.gameID, sizeof(dw.gen8.gameID));
    legacy = hash(legacy, &dw.gen8.timestamp, sizeof(dw.gen8.timestamp));
    legacy = hash(legacy, dw.gen8.licenseMD5, sizeof(dw.gen8.licenseMD5));
    legacy = hash(legacy, entry.name, strlen(entry.name));
    legacy = hash(legacy, code, sizeof(code));
    uint64_t current = legacy;
#ifdef OF_STATE_GAME_ID
    uint64_t packaged = OF_STATE_GAME_ID;
    current = hash(current, &packaged, sizeof(packaged));
#endif
    ofSavestateInit(&r);
    assert(g_gameId == current);
    check(&r, current, current, true, false, false, true);
    check(&r, legacy, legacy, true, false, false, true);
    assert(g_gameId == current); /* Loading legacy never weakens new captures. */
    check(&r, legacy ^ 1, legacy ^ 1, false, false, false, false);
    check(&r, current ^ 1, current ^ 1, false, false, false, false);
    check(&r, legacy, legacy, false, true, false, false);
    check(&r, legacy, legacy, false, false, true, false);
    check(&r, legacy, legacy ^ 1, false, false, false, true);
    check(&r, current, current ^ 1, false, false, false, true);
    if (current != legacy) {
        check(&r, legacy, current, false, false, false, true);
        check(&r, current, legacy, false, false, false, true);
    }
    dw.gen8.gameID++; ofSavestateInit(&r);
    check(&r, legacy, legacy, false, false, false, false);
    check(&r, current, current, false, false, false, false);
    puts("PASS: current and legacy identities, wrong game, damage, truncation, inner mismatch");
}
'''
globals_ = '\n'.join(re.findall(r'static uint64_t g_\w+;', source))
with tempfile.TemporaryDirectory() as directory:
    root = Path(directory)
    (root / 'test.c').write_text(pre + globals_ + '\n' + function('ofSavestateInit')
                               + '\n' + function('restoreState') + post)
    for flags in ([], ['-DOF_STATE_GAME_ID=0x93ee8d4353f0d618ULL']):
        subprocess.run(['clang', '-std=c11', '-fsanitize=address,undefined',
                        '-I' + str(repo / 'src/openfpga/platform'), *flags,
                        str(root / 'test.c'), str(repo / 'src/openfpga/platform/of_snapshot_envelope.c'),
                        '-o', str(root / 'test')], check=True)
        subprocess.run([str(root / 'test')], check=True)
