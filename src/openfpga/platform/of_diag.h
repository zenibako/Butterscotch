#ifndef OF_OF_DIAG_H
#define OF_OF_DIAG_H

#ifndef OF_PC
/* Unbuffer logs, report the heap, and halt (rather than reboot) on exit. */
void ofDiagInstall(void);
/* Print the size of a registered data file, or halt if it cannot be opened. */
void ofDiagCheckFile(const char *name);
/* Shows the text terminal with a final message and parks forever. */
void ofDiagHalt(const char *why);
#else
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
bool ofPlatformShowLogAndHalt(void);
bool ofLogDump(void);
static inline void ofDiagHalt(const char *why) {
    printf("[butterscotch] %s\n", why);
    ofLogDump(); /* as on the device, so the file can be looked at here */
    ofPlatformShowLogAndHalt();
    exit(0);
}
static inline void ofDiagInstall(void) {}
static inline void ofDiagCheckFile(const char *name) { (void) name; }
#endif

#endif /* OF_OF_DIAG_H */
