/* Asset-free checks through the real builtin registry. */
#include "vm.h"
#include "vm_builtins.h"
#include "runner.h"
#include "stb_ds.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static RValue call(VMContext *ctx, const char *name, RValue *args, int count) {
    BuiltinFunc builtin = VM_findBuiltin(ctx, name);
    assert(builtin != NULL);
    return builtin(ctx, args, count);
}

static void testDateStrings(VMContext *ctx) {
    RValue date = RValue_makeReal(46309); /* 2026-10-14, midnight UTC. */
    RValue shortDate = call(ctx, "date_date_string", &date, 1);
    RValue longDate = call(ctx, "date_datetime_string", &date, 1);
    assert(shortDate.type == RVALUE_STRING && longDate.type == RVALUE_STRING);
    assert(!strcmp(shortDate.string, "10/14/2026"));
    assert(!strcmp(longDate.string, "October 14th 2026, 00:00.00"));
    /* Keep both results alive across further calls, then release ownership. */
    RValue later = RValue_makeReal(46310);
    RValue other = call(ctx, "date_date_string", &later, 1);
    assert(!strcmp(shortDate.string, "10/14/2026"));
    assert(!strcmp(other.string, "10/15/2026"));
    RValue_free(&other);
    RValue_free(&longDate);
    RValue_free(&shortDate);
    assert(call(ctx, "date_date_string", NULL, 0).type == RVALUE_UNDEFINED);
    assert(call(ctx, "date_datetime_string", NULL, 0).type == RVALUE_UNDEFINED);
}

int main(void) {
    DataWin data = {0}; Runner runner = {0}; VMContext ctx = {0};
    data.gen8.wadVersion = 16;
    runner.dataWin = &data;
    ctx.dataWin = &data; ctx.runner = &runner;
    VMBuiltins_registerAll(&ctx);
    testDateStrings(&ctx);
    shfree(ctx.builtinMap);
    puts("PASS: owned date strings survive subsequent calls and free safely");
    return 0;
}
