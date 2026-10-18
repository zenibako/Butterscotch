"""Mode and build-ID changes must rebuild native and PC objects in one ROOT."""
import os
from pathlib import Path
import subprocess
import tempfile

port = Path(__file__).resolve().parents[1]
sdk = os.environ['SDK_ROOT']
compiler = os.environ.get('CC', 'cc')
with tempfile.TemporaryDirectory() as directory:
    root = Path(directory)
    source = root / 'fixture.c'
    source.write_text('''#include <stdio.h>
#ifndef OF_FOCUSED_BENCH
#define OF_FOCUS_BUILD_ID "ordinary"
#endif
int main(void) {
    puts(OF_SAVE_SLOT_FILE); puts(OF_LOG_SLOT_FILE); puts(OF_FOCUS_BUILD_ID);
    return 0;
}
''')
    makefile = (port / 'Makefile').read_text() + '''
.PHONY: focus-cache-fixture
focus-cache-fixture: $(OBJ_DIR)/focus-cache-check $(ROOT)/.obj/$(APP)-pc/focus-cache-check
$(OBJ_DIR)/focus-cache-check $(ROOT)/.obj/$(APP)-pc/focus-cache-check: fixture.c
\t$(PC_CC) $(BS_DEFS) $< -o $@
'''
    (root / 'Makefile').write_text(makefile)
    output = root / 'build'
    programs = [output / '.obj/deltarune/focus-cache-check',
                output / '.obj/deltarune-pc/focus-cache-check']
    def build(*settings):
        subprocess.run(['make', '--no-print-directory', 'focus-cache-fixture',
                        f'SDK_ROOT={sdk}', f'ROOT={output}', 'GAME=deltarune',
                        'USE_SDK_CONTAINER=0', f'PC_CC={compiler}', *settings],
                       cwd=root, check=True, capture_output=True, text=True)
    def expect(save, log, identity):
        for program in programs:
            actual = subprocess.check_output([str(program)], text=True).splitlines()
            assert actual == [save, log, identity], (program, actual, identity)
    build()
    expect('deltarune_0.sav', 'deltarune_1.sav', 'ordinary')
    build('FOCUS_BENCH=1', 'FOCUS_BUILD_ID=first')
    expect('deltarune_tiles_0.sav', 'deltarune_tiles_1.sav', 'first')
    build('FOCUS_BENCH=1', 'FOCUS_BUILD_ID=second')
    expect('deltarune_tiles_0.sav', 'deltarune_tiles_1.sav', 'second')
    unchanged = [p.stat().st_mtime_ns for p in programs]
    build('FOCUS_BENCH=1', 'FOCUS_BUILD_ID=second')
    assert [p.stat().st_mtime_ns for p in programs] == unchanged
    build('FOCUS_BENCH=0')
    expect('deltarune_0.sav', 'deltarune_1.sav', 'ordinary')
    build('FOCUS_BENCH=1')
    expect('deltarune_tiles_0.sav', 'deltarune_tiles_1.sav', 'unversioned')
    build()
    expect('deltarune_0.sav', 'deltarune_1.sav', 'ordinary')
print('PASS: shared ROOT rebuilds native/PC focus mode and IDs; stable config reuses objects')
