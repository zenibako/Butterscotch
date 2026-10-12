#define OF_PC 1
#include "../src/openfpga/platform/of_pc_audio.c"
#include <assert.h>
#include <string.h>
int main(void) {
    SDL_setenv("SDL_AUDIODRIVER", "dummy", 1);
    assert(SDL_Init(SDL_INIT_AUDIO) == 0);
    of_audio_init(); assert(g_audio_dev);
    SDL_PauseAudioDevice(g_audio_dev, 1);
    const int16_t abandoned[4] = {111, 222, 333, 444};
    const int16_t restored[4] = {555, 666, 777, 888};
    utPcAudioSnapshotReset();
    assert(of_audio_write(abandoned, 2) == 2);
    utPcAudioSnapshotReset();
    assert(g_audio_read_pos == g_audio_write_pos);
    assert(of_audio_write(restored, 2) == 2);
    int16_t output[4] = {0};
    audio_callback(NULL, (Uint8 *)(void *)output, sizeof(output));
    assert(!memcmp(output, restored, sizeof(output)));
    SDL_CloseAudioDevice(g_audio_dev);
    SDL_DestroyMutex(g_audio_mutex); SDL_Quit();
    puts("desktop audio queue rewind: passed");
}
