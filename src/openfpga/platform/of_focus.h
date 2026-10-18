#ifndef OF_FOCUS_H
#define OF_FOCUS_H
#include <stdbool.h>
#include <stdint.h>
struct Runner;
#ifdef OF_FOCUSED_BENCH
bool ofFocusStart(const char *argument);
void ofFocusInput(struct Runner *runner, int inputFrame);
void ofFocusPresent(struct Runner *runner, bool skipped);
void ofFocusEnd(uint64_t flipNanos);
#else
static inline bool ofFocusStart(const char *argument) { (void)argument; return false; }
static inline void ofFocusInput(struct Runner *runner, int frame) { (void)runner; (void)frame; }
static inline void ofFocusPresent(struct Runner *runner, bool skipped) { (void)runner; (void)skipped; }
static inline void ofFocusEnd(uint64_t nanos) { (void)nanos; }
#endif
#endif
