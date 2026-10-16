# Deltarune string read implementation plan

> **For agentic workers:** Use the `executing-plans` skill to implement this plan task by task. Steps use checkboxes for tracking. Delegate only if the user authorizes it.

**Goal:** Remove full-input temporary copies in `string_copy`, `string_char_at` and `string_ord_at` while preserving their current conversion, indexing and ownership behavior.

**Architecture:** Add a private borrowed-or-owned input view in `vm_builtins.c`. Existing strings are borrowed only during the synchronous call; non-strings retain `RValue_toString` conversion. Results remain independently owned and the shared `RValue_toString` API does not change.

**Tech Stack:** C, RValue, current UTF-8 helpers, asset-free builtin fixtures, ASan/UBSan, desktop and os25 replay.

**Spec:** [Shared design and verification protocol](../specs/2026-10-15-deltarune-optimization-design.md).

## Global constraints

All shared-design constraints apply. Preserve each builtin's current one-based
indexing, negative-index clamps, invalid/out-of-range behavior, null-string
handling and independent result ownership. Do not fix unrelated UTF-8 behavior
or expand the change to mutating string builtins. Keep heap operations needed
for results and non-string conversion.

### Task 1: Prove current behavior and remove input-only ownership

**Files:** Modify `src/vm_builtins.c`; create
`src/openfpga/tests/string-reads.c`; add `string-reads-fixture` to
`src/openfpga/Makefile`. Read `src/rvalue.h#RValue_toString`,
`src/text_utils.h` and `src/openfpga/tests/builtin-compat.c#call` first.

**Interfaces:** The private input view has exactly these fields and lifetime:

```c
typedef struct { const char *text; char *owned; } BuiltinStringInput;
static BuiltinStringInput builtinStringInput(RValue value, DataWin *data) {
    BuiltinStringInput input;
    if (value.type == RVALUE_STRING) {
        input.owned = NULL;
        input.text = value.string ? value.string : "";
    } else {
        input.owned = RValue_toString(value, data);
        input.text = input.owned;
    }
    return input;
}
/* Use input.text without mutation; free(input.owned) on every exit. */
```

  `RValue_toString` retains its existing allocation/failure contract for
  non-string conversions; no caller receives a borrowed conversion result.

- [ ] Add a dedicated asset-free fixture invoking the real builtin registry.
  For the existing strings, check ASCII, multibyte UTF-8, empty/null strings,
  index 0/negative/first/last/past-end, zero/negative/oversized copy length and
  malformed sequences. Check numeric/bool/undefined conversion against current
  outputs. Retain the input, call a builtin, free the input, then validate and
  free the result to prove results do not alias borrowed storage.
- [ ] Compile the fixture with allocation counting confined to its included
  `vm_builtins.c`, following the existing tile fixture's include/filter pattern:

```c
#include <stdlib.h>
#include <string.h>
static unsigned inputCopies;
static char *countedStrdup(const char *text) {
    size_t bytes = strlen(text) + 1;
    char *copy = (char *)malloc(bytes);
    assert(copy != NULL);
    memcpy(copy, text, bytes); ++inputCopies;
    return copy;
}
#define strdup countedStrdup
#include "../../vm_builtins.c"
#undef strdup
```

  Include `<assert.h>` before this block. Link existing PC objects except
  `main.o` and `up/vm_builtins.o`. Register builtins and construct arguments
  before resetting `inputCopies`; count only the selected call. Require zero
  strdup calls for nonempty string-input reads; numeric conversion must still
  allocate its temporary. The fixture must exercise the registered builtins,
  not just the new helper.
- [ ] First run behavior checks against the original implementation and record
  their outputs. Then enable the zero-input-copy assertions and require them
  to fail on the original implementation. Do not change expected semantics to
  make the optimization pass.
- [ ] Add `BuiltinStringInput` and use it only in the three named builtins.
  Replace temporary `free(str)` with `free(input.owned)` on all exits. Preserve
  existing result allocation, `strlen`/UTF-8 scans and return tags.
- [ ] Build/run the new fixture separately for each game, using fresh objects:

```sh
make -C src/openfpga GAME=deltarune PC_BIN=out/string-dr PC_OBJ_DIR=out/string-dr-obj PC_EXTRA_CFLAGS='-g -O1 -fsanitize=address,undefined -fno-sanitize=alignment,function' string-reads-fixture
DYLD_FALLBACK_LIBRARY_PATH=/opt/homebrew/lib ASAN_OPTIONS=detect_leaks=0 src/openfpga/out/string-dr_string_reads_fixture
make -C src/openfpga GAME=undertale PC_BIN=out/string-ut PC_OBJ_DIR=out/string-ut-obj PC_EXTRA_CFLAGS='-g -O1 -fsanitize=address,undefined -fno-sanitize=alignment,function' string-reads-fixture
DYLD_FALLBACK_LIBRARY_PATH=/opt/homebrew/lib ASAN_OPTIONS=detect_leaks=0 src/openfpga/out/string-ut_string_reads_fixture
```

  The new target must emit the `_string_reads_fixture` binary shown above.
  Set `data.gen8.wadVersion` to 17/16 respectively in the fixture; the existing
  builtin compatibility fixture hard-codes 16, so do not inherit that mistake.
- [ ] Run existing builtin compatibility and profiler fixtures for both games,
  plus port lint. Inspect every changed early return for ownership cleanup.
- [ ] Commit the implementation/fixtures with a GPG signature as
  `perf(vm): borrow string inputs in read-only builtins`.

### Task 2: Measure relevance to Deltarune and validate gameplay

**Files:** Private desktop/harness cases; create a dated
`src/openfpga/docs/2026-10-15-string-read-results.md` during execution.

**Interfaces:** Consume this topic's exact parent/candidate commits. Produce
allocation-count evidence, sampled builtin frequencies, channel comparisons and
a retain/reject decision independently of the audio topic.

- [ ] Use a diagnostic desktop run to count the three builtin calls and input
  lengths over the benchmark and dialogue route. Keep counters and report I/O
  out of ordinary timing builds. `BS_PROFILE` can locate scripts, but its times
  do not establish device cost and it does not report input lengths.
- [ ] Run the shared harness screen and, for a retained candidate, the complete
  two-game/mode/window matrix. Benchmark against the candidate's actual parent,
  which may already include the accepted audio change.
- [ ] Compare dialogue/menu captures using the repository verification scripts,
  both with and without native stand-ins, since a short dodge sample may not
  exercise these builtins. Preserve and compare raw PCM and game-state outputs
  where the existing desktop workflow provides them.
- [ ] Document allocations removed, actual workload frequency and simulated
  cycle results separately. If sampled workloads do not reach the changed
  builtins, label the speed effect unmeasured; synthetic allocation savings are
  not a gameplay speedup. Follow the shared gate for a small/device-only result.
- [ ] Commit the report as `docs: record string read optimization evidence`.
