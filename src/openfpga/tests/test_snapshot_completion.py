"""Exercise native request completion against retries during receive/cleanup."""
from pathlib import Path
import subprocess
import tempfile
import re
source=(Path(__file__).resolve().parents[3]/'src/openfpga/platform/ut_savestate.c').read_text()
a=source.index('bool platformSnapshotBoundary(')
b=source.index('#elif defined(OF_PC)',a)
function=source[a:b]+'}\n'
function=re.sub(r'^#if defined\(UT_NATIVE_SAVESTATES\).*\n','',function,flags=re.M)
function=re.sub(r'\*\(volatile const uint32_t \*\)\(uintptr_t\)0x4e00000(?:4|c)u','status',function)
pre=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#define OF_SAVESTATE_NONE 0
#define OF_SAVESTATE_SAVE 1
#define OF_SAVESTATE_LOAD 2
#define UT_SNAPSHOT_LIMIT 1024u
static bool g_service=true;
typedef struct{int currentRoomIndex;}Runner;
static int scenario,pending,finishes;
static uint32_t status;
static uint8_t arena[1024];
static void *allocation(size_t n){(void)n;return scenario==4?NULL:arena;}
#define malloc allocation
#define free(p) ((void)(p))
static uint64_t nowNanos(void){return 0;}
static void logInfo(const char*s,...){(void)s;}
static void captureProgress(const char*s){(void)s;}
static void utPlatformSnapshotNotice(const char*s){(void)s;}
static int of_savestate_poll(void){return pending;}
static void of_savestate_finish(int ok){(void)ok;assert(pending);pending=0;finishes++;}
static void finishLoadedState(void){of_savestate_finish(1);}
static int of_savestate_enable(uint32_t n){(void)n;return 0;}
static bool utSavestateCapture(Runner*r,uint8_t**d,size_t*n,const char**reason){
 (void)r;if(scenario==5){*reason="capture refusal";return false;}*d=arena;*n=4;return true;
}
static int of_savestate_save(const void*d,uint32_t n){
 (void)d;(void)n;of_savestate_finish(1);pending=2;return -1;
}
static int of_savestate_load(void*d,uint32_t n){
 (void)d;(void)n;if(scenario==0){of_savestate_finish(0);pending=2;return -1;}return 4;
}
static bool restoreState(Runner*r,const uint8_t*d,size_t n,const char**reason,void(*committed)(void)){
 (void)r;(void)d;(void)n;if(scenario==1){*reason="prepare refusal";return false;}
 committed();if(scenario==2)pending=1;return true;
}
'''
post=r'''
int main(void){Runner r={46};for(scenario=0;scenario<7;scenario++){
 pending=scenario>=5?1:2;finishes=0;status=pending;
 assert(platformSnapshotBoundary(&r));assert(finishes==1);
 assert(pending==((scenario==0)?2:(scenario==2)?1:(scenario==6)?2:0));
}puts("PASS: receive rejection, prepare rejection, successful load, allocation failure, capture refusal, save timeout preserve retry ownership");}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'test.c').write_text(pre+function+post)
 subprocess.run(['clang','-std=c11','-fsanitize=address,undefined',str(p/'test.c'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
