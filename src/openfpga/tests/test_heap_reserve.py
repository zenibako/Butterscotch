#!/usr/bin/env python3
"""Isolated host tests; extract production C and write binaries only to /tmp.

Run: python3 src/openfpga/tests/test_heap_reserve.py
No SDK export, game build, or shared object directory is used.
"""
from pathlib import Path
import os
import re
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[3]
OS = Path(os.environ.get('OPENFPGAOS_SOURCE', ROOT / 'src/openfpga/out/openfpgaOS-savestate'))
API = OS / 'src/firmware/api'


def function(source, name):
    match = re.search(r'^[(\w* )]+\b' + name + r'\([^\n]*\)\s*\n?\{', source, re.M)
    if match is None:
        raise AssertionError(f'missing production function {name}')
    end = source.index('\n}', match.end()) + 2
    return source[match.start():end]



def run_c(source, defines=(), arguments=((),)):
    with tempfile.TemporaryDirectory(prefix='ut-heap-reserve-') as directory:
        source_path = Path(directory) / 'test.c'
        binary = Path(directory) / 'test'
        source_path.write_text(source)
        subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror',
                        '-Wno-unused-function', '-fsanitize=undefined',
                        '-I', str(API), *defines, str(source_path), '-o', str(binary)], check=True)
        for argument in arguments:
            subprocess.run([str(binary), *argument], check=True)


class HeapReserveTests(unittest.TestCase):
    def test_real_kernel_query_bounds_tracking_and_nonmutation(self):
        source = (OS / 'src/firmware/os/kernel/syscall.c').read_text()
        self.assertTrue('int syscall_heap_can_reserve(' in source,
                      'the read-only kernel reserve query is missing')
        # Same region boundaries as the existing OS test/pc/extract.sh.
        start = source.index('#define MMAP_SLOTS')
        end = source.index('\n}', source.index('static long sys_mmap2(')) + 2
        run_c(r'''
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <errno.h>
static uintptr_t current_brk, brk_base, mmap_bottom;
#define of_term_printf(...) ((void)0)
''' + source[start:end] + r'''
static void reset(void) {
    memset(mmap_slots, 0, sizeof mmap_slots);
    mmap_slots_used = 0;
    brk_base = current_brk = 0x11000000u;
    mmap_bottom = 0x11410000u; /* exactly 4 MiB + 64 KiB */
}
static void check(uint32_t bytes, int expected) {
    mmap_slot_t before[MMAP_SLOTS];
    memcpy(before, mmap_slots, sizeof before);
    uintptr_t mb = mmap_bottom, cb = current_brk, bb = brk_base;
    int used = mmap_slots_used;
    assert(syscall_heap_can_reserve(bytes) == expected);
    assert(mmap_bottom == mb && current_brk == cb && brk_base == bb);
    assert(mmap_slots_used == used);
    assert(memcmp(before, mmap_slots, sizeof before) == 0);
}
int main(void) {
    reset(); check(0x410000u, 1); check(0x410001u, 0);
    check(0, 0); check(UINT32_MAX, 0); check(0xfffff001u, 0);
    check(0x80000000u, 0);
    mmap_bottom = current_brk + 4096; check(1, 1); check(4096, 1); check(4097, 0);
    mmap_bottom = current_brk; check(1, 0);
    mmap_bottom = current_brk - 4096; check(1, 0);
    reset(); current_brk = 0; check(1, 0);
    reset(); current_brk = brk_base - 1; check(1, 0);
    reset(); mmap_bottom = 0x40001000u; check(1, 0);
    reset(); brk_base = current_brk = 0x0ffff000u; mmap_bottom = 0x10001000u; check(8192, 0);
    reset(); mmap_bottom++; check(1, 0);
    reset(); mmap_bottom = 0x40000000u; check(4096, 1);
    reset();
    for (int i = 0; i < MMAP_SLOTS; ++i) {
        mmap_slots[i].base = 0x12000000u + (uintptr_t)i * 4096;
        mmap_slots[i].len = 4096;
        mmap_slots[i].free = 1; /* recyclable != unused tracking slot */
    }
    mmap_slots_used = MMAP_SLOTS; check(4096, 0);
    mmap_slots[MMAP_SLOTS - 1].base = 0;
    mmap_slots[MMAP_SLOTS - 1].len = 0;
    mmap_slots[MMAP_SLOTS - 1].free = 0;
    mmap_slots_used--; check(4096, 1);
    mmap_slots_used = MMAP_SLOTS + 1; check(4096, 0);
    reset(); mmap_bottom = current_brk;
    mmap_slots[0].base = mmap_bottom; mmap_slots[0].len = 0x410000;
    mmap_slots[0].free = 1; mmap_slots_used = 1;
    check(0x410000, 0); /* no coalescing to prove a positive */
    return 0;
}
''')

    def test_renderer_positive_query_skips_probe_and_fallback_keeps_eviction_policy(self):
        reserve = function((ROOT / 'src/sw/sw_texture_lru.c').read_text(), 'keepHeapReserve')
        harness = r'''
#include <stddef.h>
#include <stdbool.h>
#include <assert.h>
#define TEXTURE_CACHE_RESERVE_BYTES 4194304u
typedef struct { int unused; } SWRenderer;
static int answer, probes, releases, failures, evictions, queries;
#if defined(UT_NATIVE_SAVESTATES) && !defined(OF_PC)
static int platformHeapCanReserve(size_t bytes) {
    assert(bytes == 4194304u); ++queries; return answer;
}
#endif
static void *test_malloc(size_t bytes) {
    assert(bytes == 4194304u); ++probes;
    if (failures) { --failures; return NULL; }
    return (void *)1;
}
static void test_free(void *p) { assert(p == (void *)1); ++releases; }
static bool evictLeastRecentlyUsed(SWRenderer *swr, bool current) {
    (void)swr; assert(!current); ++evictions; return evictions < 3;
}
#define malloc test_malloc
#define free test_free
''' + reserve + r'''
int main(void) {
    SWRenderer swr = {0};
    answer = 1; keepHeapReserve(&swr);
#ifdef UT_NATIVE_SAVESTATES
#ifndef OF_PC
    assert(probes == 0 && queries == 1 && evictions == 0);
#else
    assert(probes == 1 && queries == 0);
#endif
#else
    assert(probes == 1 && queries == 0);
#endif
    probes = releases = queries = 0; answer = 0;
    keepHeapReserve(&swr); assert(probes == 1 && releases == 1 && evictions == 0);
    answer = -1; failures = 2; probes = releases = 0;
    keepHeapReserve(&swr); assert(probes == 3 && releases == 1 && evictions == 2);
    failures = 9; evictions = probes = releases = 0;
    keepHeapReserve(&swr); assert(probes == 3 && evictions == 3 && releases == 0);
    return 0;
}
'''
        for defines in (['-DUT_NATIVE_SAVESTATES'], ['-DUT_NATIVE_SAVESTATES', '-DOF_PC'], []):
            with self.subTest(defines=defines):
                run_c(harness, defines)

    def test_sdk_wrapper_rejects_missing_or_short_tables(self):
        self.assertTrue((API / 'of_heap.h').exists(), 'SDK heap wrapper is missing')
        run_c(r'''
#include <assert.h>
#include "of_heap.h"
const struct of_services_table *_of_svc_ptr;
static unsigned calls;
static int query(uint32_t n) { assert(n == 4096); ++calls; return 1; }
int main(void) {
    struct of_services_table table = {0};
    assert(of_heap_can_reserve(4096) == 0);
    _of_svc_ptr = &table;
    table.magic = OF_SVC_MAGIC; table.version = OF_SVC_VERSION;
    table.heap_can_reserve = query;
    table.count = (offsetof(struct of_services_table, heap_can_reserve) - 12u) / sizeof(void *);
    assert(of_heap_can_reserve(4096) == 0 && calls == 0);
    ++table.count;
    assert(of_heap_can_reserve(4096) == 1 && calls == 1);
    table.heap_can_reserve = NULL;
    assert(of_heap_can_reserve(4096) == 0 && calls == 1);
    table.heap_can_reserve = query; table.magic = 0;
    assert(of_heap_can_reserve(4096) == 0 && calls == 1);
    table.magic = OF_SVC_MAGIC; table.version = 0;
    assert(of_heap_can_reserve(4096) == 0 && calls == 1);
    return 0;
}
''')
        run_c('#include "of_heap.h"\n#include <assert.h>\n'
              'int main(void) { assert(of_heap_can_reserve(4096) == 0); }', ['-DOF_PC'])

    def test_platform_headroom_and_one_original_probe(self):
        source = (ROOT / 'src/openfpga/platform/of_platform.c').read_text()
        self.assertTrue('int platformHeapCanReserve(' in source, 'platform hook is missing')
        run_c(r'''
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdbool.h>
static unsigned probes, frees, queries, logs, ticks;
static int answer = 1, fail_probe;
static uint64_t nowNanos(void) { return ++ticks * 1000000ull; }
static int of_heap_can_reserve(uint32_t bytes) {
    assert(bytes == 4259840u); ++queries; return answer;
}
static void *probe_malloc(size_t bytes) {
    assert(bytes == 4194304u); ++probes; return fail_probe ? NULL : (void *)1;
}
static void probe_free(void *ptr) { assert(ptr == (void *)1); ++frees; }
#define malloc probe_malloc
#define free probe_free
static void logInfo(const char *format, ...) { (void)format; ++logs; }
''' + function(source, 'platformHeapCanReserve') + r'''
int main(int argc, char **argv) {
    (void)argv; fail_probe = argc == 2;
    if (argc == 3) answer = 0;
    assert(platformHeapCanReserve(4194304u) == !fail_probe);
    assert(probes == 1 && frees == (unsigned)!fail_probe && queries == 1 && logs > 0);
    unsigned measured = ticks;
    assert(platformHeapCanReserve(4194304u) == answer);
    assert(probes == 1 && queries == 2 && ticks == measured);
    answer = 0; assert(platformHeapCanReserve(4194304u) == 0);
    answer = -1; assert(platformHeapCanReserve(4194304u) == 0);
    unsigned queried = queries;
    assert(platformHeapCanReserve(SIZE_MAX) == 0);
    assert(platformHeapCanReserve(UINT32_MAX - 65535u) == 0);
    assert(platformHeapCanReserve(0) == 0 && queries == queried);
    return 0;
}
''', ['-DUT_NATIVE_SAVESTATES'], [[], ['fail-original-probe'], ['old', 'firmware']])


if __name__ == '__main__':
    unittest.main()
