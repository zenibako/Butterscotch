//go:build wasip1

package main

import (
	"github.com/extism/go-pdk"
	"github.com/zenibako/Butterscotch/pocket-data/plugin"
)

//go:wasmimport extism:host/user print_msg
func printMsg(uint64) uint64

func progress(message string) {
	memory := pdk.AllocateString(message + "\n")
	defer memory.Free()
	printMsg(memory.Offset())
}

var session = plugin.New(pocketRoot(), progress)

func output(data []byte, err error) int32 {
	if err != nil {
		pdk.SetError(err)
		return 1
	}
	pdk.Output(data)
	return 0
}

//go:wasmexport start
func start() int32 { return output(session.Start()) }

//go:wasmexport handle_response
func handleResponse() int32 { return output(session.Handle(pdk.Input())) }

func main() {}

// TinyGo normalizes Pocket Sync's relative preopens into absolute guest paths.
// Keep the mount explicit instead of depending on which preopen becomes cwd.
func pocketRoot() string { return "/pocket" }
