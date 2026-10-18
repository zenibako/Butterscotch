package integration

import (
	"os/exec"
	"path/filepath"
	"testing"

	"github.com/zenibako/Butterscotch/pocket-data/internal/testgame"
	"github.com/zenibako/Butterscotch/pocket-data/prep"
)

// The C converters remain an independent oracle, not a runtime dependency.
func TestCReference(t *testing.T) {
	python, err := exec.LookPath("python3")
	if err != nil {
		t.Skip("C oracle needs developer Python and a host C compiler")
	}
	if _, err = exec.LookPath("cc"); err != nil {
		t.Skip("C oracle needs a host C compiler")
	}
	root := t.TempDir()
	tools := filepath.Join(root, "tools")
	script, err := filepath.Abs("../../prepare_game.py")
	if err != nil {
		t.Fatal(err)
	}
	cmd := exec.Command(python, script, "--build-tools", "--tools-dir", tools)
	if out, err := cmd.CombinedOutput(); err != nil {
		t.Fatalf("build C oracle: %v\n%s", err, out)
	}
	for _, game := range []string{"undertale", "deltarune"} {
		t.Run(game, func(t *testing.T) {
			source := filepath.Join(root, game)
			if err := testgame.Write(source, game); err != nil {
				t.Fatal(err)
			}
			addVorbis(t, source)
			cOut := filepath.Join(root, game+"-c")
			gOut := filepath.Join(root, game+"-go")
			cmd := exec.Command(python, script, "--game", game, "--source", source, "--output", cOut, "--tools-dir", tools)
			if out, err := cmd.CombinedOutput(); err != nil {
				t.Fatalf("C reference: %v\n%s", err, out)
			}
			if _, err := prep.Prepare(prep.Options{Game: game, Source: source, Output: gOut}); err != nil {
				t.Fatal(err)
			}
			compareAssets(t, cOut, gOut)
		})
	}
}
