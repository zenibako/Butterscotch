#ifndef SW_CALL_NOTES_H
#define SW_CALL_NOTES_H

#include <stdbool.h>
#include <stdint.h>

// What individual draw calls cost, for a benchmark to print: the profile's
// totals per kind of call say that rectangles took 20 ms, these say which
// rectangles. Calls are grouped by a short description of what was asked for
// (kind, size, opacity), and time is summed per description. Only kept while
// swrCallNotes is set, and only in builds with SW_DRAW_PROFILE.
typedef struct {
    char what[56];
    uint32_t calls;
    uint64_t nanos;
} SWCallNote;

extern bool swrCallNotes;

// Whether draw calls are timed at all (SW_DRAW_PROFILE builds): the platform
// may turn it off for frames nobody will ask about. Notes are only taken of
// timed calls.
extern bool swrDrawTimed;

// Copies out the descriptions with the most time, heaviest first, and starts
// over. *missed is how many calls were dropped: when the table of descriptions is
// full, the one with the least time makes way for a new one.
int swrCallNotesTake(SWCallNote* out, int max, uint32_t* missed);


// See sw_renderer.c.
struct Renderer;
void swrSpriteCostProbe(struct Renderer* renderer, int calls, int32_t* width, int32_t* height, uint32_t* outside, uint32_t* inside);

#endif
