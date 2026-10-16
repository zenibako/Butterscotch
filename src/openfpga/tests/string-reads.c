/* Asset-free checks through the real registry. Count strdup only in this
 * translation unit's builtin implementation, including its inline helpers. */
#include <assert.h>
#include <stdlib.h>
#include <string.h>
static unsigned inputCopies;
static char *countedStrdup(const char *text) {
    size_t bytes = strlen(text) + 1;
    char *copy = (char *)malloc(bytes);
    assert(copy != NULL);
    memcpy(copy, text, bytes);
    ++inputCopies;
    return copy;
}
#define strdup countedStrdup
#include "../../vm_builtins.c"
#undef strdup

static unsigned checked, allocationFailures;
static bool behaviorOnly;

static RValue call(VMContext *ctx, const char *name, RValue *args, int count,
                   unsigned expectedCopies) {
    BuiltinFunc builtin = VM_findBuiltin(ctx, name);
    assert(builtin != NULL);
    inputCopies = 0;
    RValue result = builtin(ctx, args, count);
    if (!behaviorOnly && inputCopies != expectedCopies) {
        fprintf(stderr, "%s: expected %u strdup calls, got %u\n",
                name, expectedCopies, inputCopies);
        ++allocationFailures;
    }
    ++checked;
    return result;
}

static void expectString(RValue *result, const char *expected) {
    assert(result->type == RVALUE_STRING);
    assert(result->gmlStackType == GML_TYPE_STRING);
    assert(result->ownsReference);
    assert(result->string != NULL && !strcmp(result->string, expected));
    RValue_free(result);
}

static void expectOrdinal(RValue *result, int expected) {
    assert(result->type == RVALUE_REAL);
    assert(result->gmlStackType == GML_TYPE_DOUBLE);
    assert(result->real == (GMLReal)expected);
    RValue_free(result);
}

typedef struct {
    const char *text;
    int index, length;
    const char *character;
    int ordinal;
    const char *copy;
} StringCase;

/* Literal expectations preserve the current decoder, including malformed UTF-8
 * and its different byte ranges for char_at and copy. */
static const StringCase stringCases[] = {
    {"ABC", -3, 2, "A", 65, "AB"},
    {"ABC", 0, 2, "A", 65, "AB"},
    {"ABC", 1, 2, "A", 65, "AB"},
    {"ABC", 2, 1, "B", 66, "B"},
    {"ABC", 3, 9, "C", 67, "C"},
    {"ABC", 4, 2, "", -1, ""},
    {"ABC", 99, 2, "", -1, ""},
    {"ABC", 1, 0, "A", 65, ""},
    {"ABC", 1, -2, "A", 65, ""},
    {"ABC", 1, 99, "A", 65, "ABC"},
    {"", 1, 1, "", -1, ""},
    {"", 0, 1, "", -1, ""},
    {"", -3, 0, "", -1, ""},
    {NULL, 1, 1, "", -1, ""},
    {NULL, 0, 1, "", -1, ""},
    {NULL, -3, -1, "", -1, ""},
    {"A\xc3\xa9\xe2\x82\xac\xf0\x9f\x98\x80Z", 0, 2, "A", 65, "A\xc3\xa9"},
    {"A\xc3\xa9\xe2\x82\xac\xf0\x9f\x98\x80Z", 2, 2, "\xc3\xa9", 233, "\xc3\xa9\xe2\x82\xac"},
    {"A\xc3\xa9\xe2\x82\xac\xf0\x9f\x98\x80Z", 3, 1, "\xe2\x82\xac", 8364, "\xe2\x82\xac"},
    {"A\xc3\xa9\xe2\x82\xac\xf0\x9f\x98\x80Z", 4, 1, "\xf0\x9f\x98\x80", 65533, "\xf0\x9f\x98\x80"},
    {"A\xc3\xa9\xe2\x82\xac\xf0\x9f\x98\x80Z", 5, 99, "Z", 90, "Z"},
    {"A\xc3\xa9\xe2\x82\xac\xf0\x9f\x98\x80Z", 6, 1, "", -1, ""},
    {"A\xc3\xa9\xe2\x82\xac\xf0\x9f\x98\x80Z", 99, 1, "", -1, ""},
    {"\xe2\x82X", 1, 1, "\xe2", 65533, "\xe2\x82"},
    {"\xe2\x82X", 2, 1, "X", 88, "X"},
    {"\xe2\x82", 1, 1, "\xe2", 65533, "\xe2\x82"},
    {"\x80\x81Z", 1, 1, "\x80", 65533, "\x80\x81"},
    {"\x80\x81Z", 2, 1, "Z", 90, "Z"},
    {"\xc0\xafQ", 1, 1, "\xc0\xaf", 47, "\xc0\xaf"},
    {"\xed\xa0\x80Q", 1, 1, "\xed\xa0\x80", 55296, "\xed\xa0\x80"},
    {"\xffQ", 1, 1, "\xff", 65533, "\xff"},
};

static RValue ownedInput(const char *text) {
    return text ? RValue_makeOwnedString(strdup(text)) : RValue_makeString(NULL);
}

static void testStrings(VMContext *ctx) {
    for (size_t i = 0; i < sizeof(stringCases) / sizeof(stringCases[0]); ++i) {
        const StringCase *test = &stringCases[i];
        RValue args[] = {ownedInput(test->text), RValue_makeInt32(test->index),
                         RValue_makeInt32(test->length)};
        RValue result = call(ctx, "string_copy", args, 3, test->copy[0] ? 0 : 1);
        RValue_free(&args[0]);
        expectString(&result, test->copy);

        args[0] = ownedInput(test->text);
        result = call(ctx, "string_char_at", args, 2, test->character[0] ? 0 : 1);
        RValue_free(&args[0]);
        expectString(&result, test->character);

        args[0] = ownedInput(test->text);
        result = call(ctx, "string_ord_at", args, 2, 0);
        RValue_free(&args[0]);
        expectOrdinal(&result, test->ordinal);
    }
}

static void testConversion(VMContext *ctx, RValue input, const char *text) {
    RValue args[] = {input, RValue_makeInt32(1), RValue_makeInt32(99)};
    RValue result = call(ctx, "string_copy", args, 3, 1);
    expectString(&result, text);
    char first[] = {text[0], '\0'};
    result = call(ctx, "string_char_at", args, 2, 1);
    expectString(&result, first);
    result = call(ctx, "string_ord_at", args, 2, 1);
    expectOrdinal(&result, (unsigned char)text[0]);
    /* Exercise cleanup of converted temporaries on each post-conversion exit. */
    args[1] = RValue_makeInt32(99);
    result = call(ctx, "string_copy", args, 3, 2);
    expectString(&result, "");
    result = call(ctx, "string_char_at", args, 2, 2);
    expectString(&result, "");
    result = call(ctx, "string_ord_at", args, 2, 1);
    expectOrdinal(&result, -1);
}

static void testMissingArguments(VMContext *ctx) {
    RValue args[] = {RValue_makeString("ABC"), RValue_makeInt32(1)};
    for (int count = 0; count < 3; ++count) {
        RValue result = call(ctx, "string_copy", args, count, 1);
        expectString(&result, "");
        if (count < 2) {
            result = call(ctx, "string_char_at", args, count, 1);
            expectString(&result, "");
            result = call(ctx, "string_ord_at", args, count, 0);
            expectOrdinal(&result, -1);
        }
    }
}

static void testAllocationProbes(VMContext *ctx) {
    const char *names[] = {"string_copy", "string_char_at", "string_ord_at"};
    RValue args[] = {ownedInput("borrow me"), RValue_makeInt32(1), RValue_makeInt32(3)};
    for (int i = 0; i < 3; ++i) {
        RValue result = call(ctx, names[i], args, i == 0 ? 3 : 2, 0);
        printf("%s string input: %u strdup calls\n", names[i], inputCopies);
        RValue_free(&result);
    }
    RValue_free(&args[0]);
    args[0] = RValue_makeReal(123);
    for (int i = 0; i < 3; ++i) {
        RValue result = call(ctx, names[i], args, i == 0 ? 3 : 2, 1);
        printf("%s numeric input: %u strdup calls\n", names[i], inputCopies);
        RValue_free(&result);
    }
}

int main(int argc, char **argv) {
    DataWin data = {0};
    Runner runner = {0};
    VMContext ctx = {0};
    behaviorOnly = argc == 2 && !strcmp(argv[1], "--behavior-only");
#ifdef GAME_DELTARUNE
    data.gen8.wadVersion = 17;
#else
    data.gen8.wadVersion = 16;
#endif
    runner.dataWin = &data;
    ctx.dataWin = &data;
    ctx.runner = &runner;
    VMBuiltins_registerAll(&ctx);
    testStrings(&ctx);
    testConversion(&ctx, RValue_makeReal(42), "42");
    testConversion(&ctx, RValue_makeReal(3.5), "3.50");
    testConversion(&ctx, RValue_makeInt32(-23), "-23");
    testConversion(&ctx, RValue_makeInt64(123), "123");
    testConversion(&ctx, RValue_makeBool(true), "1");
    testConversion(&ctx, RValue_makeBool(false), "0");
    testConversion(&ctx, RValue_makeUndefined(), "undefined");
    testMissingArguments(&ctx);
    testAllocationProbes(&ctx);
    shfree(ctx.builtinMap);
    printf("WAD %u: %u behavior/ownership checks, %u allocation failures (%s)\n",
           (unsigned)data.gen8.wadVersion, checked, allocationFailures,
           behaviorOnly ? "behavior only" : "allocation assertions enabled");
    return allocationFailures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
