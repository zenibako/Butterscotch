"""Compile the production probe with controlled scene/resource verdicts."""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest

SOURCE = (Path(__file__).resolve().parents[1] / "platform/of_state_requests.c").read_text()


def function(signature):
    start = SOURCE.index(signature)
    body = SOURCE.index("{", start)
    end, depth = body + 1, 1
    while depth:
        depth += (SOURCE[end] == "{") - (SOURCE[end] == "}")
        end += 1
    return SOURCE[start:end]


PRE = r'''
#define _POSIX_C_SOURCE 200809L
#define OF_PC
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef struct {const char *name;} Room;
typedef struct {struct {unsigned count; Room *rooms;} room;} DataWin;
typedef struct {int frameCount,currentRoomIndex;Room *currentRoom;DataWin *dataWin;} Runner;
static bool g_probeCapture;
static int blockerCount;
static char reasons[24][100];
static const char *sceneReason;
static const char *scene(Runner *r) {(void)r;return sceneReason;}
static struct {const char *(*savestateReason)(Runner *);} ofGame={scene};
static const char *Runner_snapshotCheck(Runner *r) {(void)r;return blockerCount?reasons[0]:NULL;}
static int Runner_snapshotBlockers(Runner *r,const char **out,int max) {
 (void)r;for(int i=0;i<blockerCount && i<max;i++)out[i]=reasons[i];return blockerCount;
}
static bool ofSavestateCapture(Runner *r,uint8_t **data,size_t *size,const char **failed) {
 (void)r;(void)failed;assert(g_probeCapture);*data=NULL;*size=123;return true;
}
'''

POST = r'''
int main(int argc,char **argv) {
 assert(argc==2);Room room={"fixture"};DataWin dw={{1,&room}};Runner r={30,0,&room,&dw};
 for(int i=0;i<24;i++)snprintf(reasons[i],sizeof reasons[i],"blocker %02d with a sufficiently long description to exercise untruncated diagnostic output",i);
 if(!strcmp(argv[1],"bypass")) {
  unsetenv("BS_STATE_PROBE_BYPASS");assert(!policyBypassed());
  setenv("BS_STATE_PROBE_BYPASS","0",1);assert(!policyBypassed());
  setenv("BS_STATE_PROBE_BYPASS","",1);assert(!policyBypassed());
  setenv("BS_STATE_PROBE_BYPASS","1",1);assert(policyBypassed());
  unsetenv("BS_STATE_PROBE_BYPASS");g_probeCapture=true;assert(policyBypassed());
  return 0;
 }
 blockerCount=!strcmp(argv[1],"many")?24:1;
 assert(ofSavestateReason(&r)==reasons[0]);
 sceneReason="scene refused";assert(!strcmp(ofSavestateReason(&r),sceneReason));sceneReason=NULL;
 setenv("BS_STATE_PROBE","30",1);stateProbe(&r);
 return 0;
}
'''


class StateProbeTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.directory = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.directory.cleanup)
        root = Path(cls.directory.name)
        source = root / "probe.c"
        source.write_text(PRE + "\n".join(function(signature) for signature in [
            "static bool policyBypassed(", "static const char *policyReason(",
            "const char *ofSavestateReason(", "static void stateProbe(",
        ]) + POST)
        cls.binary = root / "probe"
        subprocess.run([os.environ.get("CC", "clang"), "-std=c11", "-fsanitize=address,undefined",
                        str(source), "-o", str(cls.binary)], check=True)

    def run_probe(self, scenario):
        result = subprocess.run([str(self.binary), scenario], text=True, capture_output=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        return result.stderr.strip().split("\t")

    def test_scene_policy_does_not_include_serializer_refusals(self):
        fields = self.run_probe("policy")
        self.assertEqual(fields[4], "allowed")
        self.assertEqual(fields[5], "refused")
        self.assertIn("blocker 00", fields[7])

    def test_reports_more_than_sixteen_long_blockers_without_truncation(self):
        fields = self.run_probe("many")
        self.assertEqual(len(fields[7].split(" | ")), 24)
        for i in range(24):
            self.assertIn(f"blocker {i:02d} with a sufficiently long description to exercise untruncated diagnostic output", fields[7])

    def test_bypass_requires_explicit_one(self):
        self.run_probe("bypass")


if __name__ == "__main__":
    unittest.main()
