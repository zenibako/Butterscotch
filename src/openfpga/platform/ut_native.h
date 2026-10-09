#ifndef UT_NATIVE_H
#define UT_NATIVE_H

#include "runner.h"

/* Installs the native stand-ins for the game scripts that cost the most to
 * interpret (see ut_native.c). Each goes in only if the game's bytecode for
 * it is exactly the one it was written against. */
void utNativeInstall(Runner *runner);

#endif /* UT_NATIVE_H */
