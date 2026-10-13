#define OF_PC 1
#include "../src/openfpga/platform/of_audio_system.c"
#include "../src/openfpga/platform/ut_snapshot_parts.h"
#include <assert.h>

void logWarn(const char *fmt, ...) { (void)fmt; }
void logError(const char *fmt, ...) { (void)fmt; abort(); }
void logInfo(const char *fmt, ...);
uint64_t nowNanos(void) { return 0; }
bool utPerfDetail = false;
void utPerfAddLoad(UtLoadKind kind, uint64_t value) { (void)kind; (void)value; }
void utPerfPhase(UtPhase phase) { (void)phase; }
#include <stdarg.h>
static char lastInfo[512];
void logInfo(const char *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    vsnprintf(lastInfo, sizeof(lastInfo), fmt, ap); va_end(ap);
}
static int freePairs;
static int16_t submitted[32];
static int submittedCount;
static bool stateRequestPending;
static int deferAfterChecks = -1;
bool utSavestateDeferFileReads(void) {
    return stateRequestPending || (deferAfterChecks >= 0 && deferAfterChecks-- == 0);
}
int of_audio_free(void) { return freePairs; }
void of_audio_init(void) { submittedCount = 0; }
void utPcAudioSnapshotReset(void) { submittedCount = 0; }
int of_audio_write(const int16_t *samples, int count) {
    assert(count <= 16);
    memcpy(submitted, samples, (size_t)count * 4);
    submittedCount = count;
    return count;
}

int main(void) {
    UtAudioSystem ut = {0};
    UtMusicTrack track = {0};
    uint8_t adpcm[64];
    for (unsigned i = 0; i < sizeof(adpcm); i++) adpcm[i] = (uint8_t)(i * 19);
    strcpy(track.name, "test");
    track.sampleCount = 128; track.sampleRate = 32000;
    ut.tracks = &track; ut.trackCount = 1; ut.masterGain = 1;
    ut.file = tmpfile(); assert(ut.file);
    assert(fwrite(adpcm, 1, sizeof(adpcm), ut.file) == sizeof(adpcm));
    ut.cachedSounds = calloc(1, sizeof(uint8_t *));
    ut.cachedLastUse = calloc(1, sizeof(uint32_t));
    ut.cachedSounds[0] = malloc(sizeof(adpcm));
    memcpy(ut.cachedSounds[0], adpcm, sizeof(adpcm));
    ut.cachedBytes = sizeof(adpcm);
    UtVoice *v = &ut.voices[0];
    v->active = true; v->streamSlot = -1; v->data = ut.cachedSounds[0];
    v->bufferLen = sizeof(adpcm); v->step = 43700; v->frac = 65536;
    v->instanceId = 100012; v->sourceIndex = 7; v->gainNow = 4096 << 8;
    v->gainTarget = 2000 << 8; v->gainStepPerPair = 100; v->pitch = 1;
    ut.voices[1] = *v;
    ut.voices[1].streamSlot = 0;
    ut.voices[1].instanceId = 100011;
    ut.voices[1].fileBytePos = sizeof(adpcm);
    memcpy(ut.streamBuffers[0], adpcm, sizeof(adpcm));
    ut.voices[1].data = ut.streamBuffers[0];
    ut.nextInstanceId = 100013;
    adpcmDiffInit();
    int32_t prefix[9] = {0}, expected[30] = {0}, actual[30] = {0};
    mixVoice(&ut, v, prefix, 9);
    mixVoice(&ut, &ut.voices[1], prefix, 9);
    uint8_t *bytes = NULL; size_t size = 0; const char *reason = NULL;
    assert(utAudioSnapshot(&ut.base, &bytes, &size, &reason));
    mixVoice(&ut, v, expected, 30);
    mixVoice(&ut, &ut.voices[1], expected, 30);
    void *p = utAudioPrepareSnapshot(&ut.base, bytes, size, &reason);
    assert(p);
    utAudioApplySnapshot(&ut.base, p); utAudioFreeSnapshot(p);
    mixVoice(&ut, &ut.voices[0], actual, 30);
    mixVoice(&ut, &ut.voices[1], actual, 30);
    assert(memcmp(expected, actual, sizeof(actual)) == 0);
    /* A Pocket state request must reach the frame boundary without issuing
     * another music read. Preserve read-ahead and decoder state until then. */
    UtVoice deferred = ut.voices[1];
    deferred.bufferPos = 4; deferred.bufferLen = 12; deferred.fileBytePos = 12;
    uint8_t beforeBuffer[64]; memcpy(beforeBuffer, deferred.data, sizeof(beforeBuffer));
    UtVoice beforeVoice = deferred;
    stateRequestPending = true;
    refillVoice(&ut, &deferred);
    assert(memcmp(&beforeVoice, &deferred, sizeof(deferred)) == 0);
    assert(memcmp(beforeBuffer, deferred.data, sizeof(beforeBuffer)) == 0);
    stateRequestPending = false;
    refillVoice(&ut, &deferred);
    assert(deferred.fileBytePos == sizeof(adpcm));
    assert(deferred.bufferPos == 0 && deferred.bufferLen == sizeof(adpcm) - 4);
    /* A request can arrive after the refill entry check. Do not start a
     * fallback file read; retaining the compacted unread tail is sufficient. */
    deferred = beforeVoice;
    memcpy(deferred.data, beforeBuffer, sizeof(beforeBuffer));
    deferAfterChecks = 1;
    refillVoice(&ut, &deferred);
    assert(deferred.fileBytePos == beforeVoice.fileBytePos);
    assert(deferred.bufferPos == 0 && deferred.bufferLen == 8);
    deferAfterChecks = -1;
    refillVoice(&ut, &deferred);
    assert(deferred.fileBytePos == sizeof(adpcm));
    assert(ut.voices[0].instanceId == 100012 && ut.nextInstanceId == 100013);
    for (size_t n = 0; n < size; n++)
        assert(utAudioPrepareSnapshot(&ut.base, bytes, n, &reason) == NULL);
    uint8_t id[4]; memcpy(id, bytes + 12, 4);
    memset(bytes + 12, 0xff, 4); bytes[15] = 0x7f;
    assert(utAudioPrepareSnapshot(&ut.base, bytes, size, &reason) == NULL);
    memcpy(bytes + 12, id, 4);
    free(bytes);
    ut.queueCapacity = 8; freePairs = 3; ut.snapshotWritten = 8;
    ut.snapshotPcm = calloc(16, sizeof(int16_t));
    for (unsigned i = 0; i < 16; i++) ut.snapshotPcm[i] = (int16_t)(i * 100);
    assert(utAudioSnapshot(&ut.base, &bytes, &size, &reason));
    p = utAudioPrepareSnapshot(&ut.base, bytes, size, &reason); assert(p);
    assert(((UtAudioSnapshot *)p)->pcmPairs == 5);
    utAudioApplySnapshot(&ut.base, p); utAudioFreeSnapshot(p);
    assert(submittedCount == 5 && submitted[0] == 600 && submitted[9] == 1500);
    free(bytes); bytes = NULL;
    /* Pocket's inactive capacity is 8; an active stream reserves one pair,
     * hence free=2 means five unread pairs, not six. */
    ut.snapshotReservedPairs = 1; ut.snapshotWritten = 8; freePairs = 2;
    for (unsigned i = 0; i < 16; i++) ut.snapshotPcm[i] = (int16_t)(i * 100);
    assert(utAudioSnapshot(&ut.base, &bytes, &size, &reason));
    p = utAudioPrepareSnapshot(&ut.base, bytes, size, &reason); assert(p);
    assert(((UtAudioSnapshot *)p)->pcmPairs == 5);
    utAudioApplySnapshot(&ut.base, p); utAudioFreeSnapshot(p);
    assert(submittedCount == 5 && submitted[0] == 600 && submitted[9] == 1500);
    /* Two more capture/restore cycles preserve the unread PCM and decoder,
     * including Pocket's reserved pair. The host's read pointer stays put. */
    for (unsigned cycle = 0; cycle < 2; cycle++) {
        free(bytes); bytes = NULL;
        assert(utAudioSnapshot(&ut.base, &bytes, &size, &reason));
        p = utAudioPrepareSnapshot(&ut.base, bytes, size, &reason); assert(p);
        assert(((UtAudioSnapshot *)p)->pcmPairs == 5);
        utAudioApplySnapshot(&ut.base, p); utAudioFreeSnapshot(p);
        assert(submittedCount == 5 && submitted[0] == 600 && submitted[9] == 1500);
    }
    /* Reject an impossible queue with enough evidence to distinguish hardware
     * accounting from voice validity and allocation failure. */
    free(bytes); bytes = NULL;
    ut.snapshotWritten = 3; freePairs = 0;
    assert(!utAudioSnapshot(&ut.base, &bytes, &size, &reason));
    assert(strstr(lastInfo, "queue shadow") && strstr(lastInfo, "written 3"));
    free(ut.snapshotPcm);
    free(bytes); free(ut.cachedSounds[0]); free(ut.cachedSounds); free(ut.cachedLastUse);
    fclose(ut.file);
    puts("audio decoder snapshot: passed");
}
