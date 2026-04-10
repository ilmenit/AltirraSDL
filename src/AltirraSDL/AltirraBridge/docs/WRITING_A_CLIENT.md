# Writing an AltirraBridge client in any language

**The wire protocol is the SDK.** AltirraBridge talks
newline-delimited JSON over a plain TCP socket. Any language with
sockets and a JSON parser — which is every language — can drive the
emulator natively, with no FFI, no shared libraries, no C toolchain,
no `unsafe`, and no ABI compatibility to track.

The Python, C, and Free Pascal SDKs shipped in this package are
convenience wrappers for their respective languages. They are
**not** the API. The API is what this document describes. A
~100-line native client in the language of your choice will be
nicer to use than any FFI shim over the C SDK, and shorter to
write than you probably expect.

> **Targeting Python, C, C++, or Free Pascal?** Don't follow this
> guide — use the ready-made SDK at `sdk/python/`, `sdk/c/`, or
> `sdk/pascal/` respectively. Each one hands you a typed client
> class / struct with every command already wrapped, examples you
> can copy from, and a dedicated README. This guide is for
> languages we don't ship an SDK for.

This guide walks through writing a minimal client from scratch. At
the end there are two complete, runnable reference implementations:
**Rust** and **Go**. Each is self-contained, uses only the standard
library of its language, and fits on a single screen. (The approach
ports straight to Zig, Nim, Lua, Ruby, Crystal, D, Swift, Kotlin,
C#, Java, and every other language with sockets and a JSON parser.)

## The protocol in ten bullets

1. Transport is **TCP loopback**, always `127.0.0.1`. (POSIX Unix
   domain sockets are also supported on Linux/macOS; the minimal
   examples below use TCP because it works everywhere.)
2. On startup the server writes a **token file**
   (`/tmp/altirra-bridge-<pid>.token` on POSIX, `%TEMP%\…` on
   Windows). It contains exactly two lines:

   ```
   tcp:127.0.0.1:54321
   9ec0...e4
   ```

   Line 1 is the bound address in `tcp:HOST:PORT` form. Line 2 is a
   32-character hex session token. Your client reads both.
3. Connect a plain TCP socket to `HOST:PORT`.
4. **First command must be `HELLO <token>`.** Until HELLO succeeds,
   every other command returns `{"ok":false,"error":"auth required"}`
   and is dropped.
5. A command is a single line of UTF-8 text terminated by `\n`:
   `VERB ARG1 ARG2…\n`. Numeric arguments accept decimal (`60`),
   C-hex (`0x3c`), or Atari-hex (`$3c`).
6. A response is a single line of UTF-8 JSON terminated by `\n`. It
   is always one JSON object, and it always contains an `"ok"`
   boolean. Success responses carry whatever payload the command
   produces. Error responses carry `"error"`.
7. **Request/response is strictly serial.** Send one command, read
   one response, then send the next. Do not pipeline. The server
   runs inside the SDL3 main loop and processes exactly one command
   at a time.
8. There are no server-initiated messages, no keep-alives, no
   out-of-band frames. An empty line is a no-op and produces no
   response (handy if you need a keep-alive — just flush `\n`).
9. Responses can be large (a memory dump or an instruction history
   can be hundreds of kilobytes on one line). **Read until `\n`, not
   into a fixed buffer.** Every language's line reader does this
   right by default; don't second-guess it.
10. The protocol is versioned. `HELLO` returns `{"ok":true,
    "protocol":1,…}`. Refuse to talk to a server whose major version
    is higher than yours.

That's the whole protocol surface you need to hit `PING` / `FRAME` /
`PEEK` / `POKE` / `SCREENSHOT` / `REGS` / the debugger / everything.
The per-command reference lives in [`COMMANDS.md`](COMMANDS.md) and
the exhaustive spec in [`PROTOCOL.md`](PROTOCOL.md).

## Do I need a real JSON parser?

For a throwaway script: no. The responses are small enough that a
substring check like `contains("\"ok\":true")` works fine. The
examples below do exactly that.

For production code: yes, use your language's standard JSON parser.
The response objects have stable field names and predictable shapes.
Anything the C or Python SDK parses, your client can parse the same
way.

## Minimal client: Rust

Single file, stdlib only, no Cargo dependencies. Build with
`rustc altirra_client.rs` or drop it into a Cargo binary crate.

```rust
// altirra_client.rs — minimal AltirraBridge client in stdlib Rust.
//
// Usage: altirra_client <token-file>

use std::env;
use std::fs;
use std::io::{BufRead, BufReader, Write};
use std::net::TcpStream;
use std::process;

fn main() -> std::io::Result<()> {
    let args: Vec<String> = env::args().collect();
    if args.len() != 2 {
        eprintln!("usage: {} <token-file>", args[0]);
        process::exit(2);
    }

    // Token file: two lines — "tcp:HOST:PORT" and a 32-hex token.
    let contents = fs::read_to_string(&args[1])?;
    let mut lines = contents.lines();
    let addr_line = lines.next().unwrap_or("").trim();
    let token     = lines.next().unwrap_or("").trim();

    let addr = addr_line
        .strip_prefix("tcp:")
        .expect("this example only handles tcp: addresses");

    let stream = TcpStream::connect(addr)?;
    let mut reader = BufReader::new(stream.try_clone()?);
    let mut writer = stream;

    // One synchronous request/response round-trip.
    let mut rpc = |cmd: &str| -> std::io::Result<String> {
        writer.write_all(cmd.as_bytes())?;
        writer.write_all(b"\n")?;
        let mut line = String::new();
        reader.read_line(&mut line)?;
        Ok(line.trim_end_matches(&['\r', '\n'][..]).to_string())
    };

    // HELLO is mandatory and must come first.
    let hello = rpc(&format!("HELLO {}", token))?;
    println!("HELLO -> {hello}");
    if !hello.contains("\"ok\":true") {
        eprintln!("authentication failed");
        process::exit(1);
    }

    println!("PING  -> {}", rpc("PING")?);
    println!("FRAME -> {}", rpc("FRAME 60")?);
    println!("PING  -> {}", rpc("PING")?);

    Ok(())
}
```

To turn this into a real SDK, wrap the `rpc` closure in a struct
with a method per command, swap the substring check for `serde_json`
parsing, and add a `Drop` impl that cleanly closes the socket. The
whole wrapper is still well under 500 lines.

## Free Pascal note

Free Pascal users should use the full SDK at
[`../sdk/pascal/`](../sdk/pascal/) — it ships `TAltirraBridge`
with every Phase 1-4 command already wrapped, exception-based
error handling, a `TCpuState` record, a `TRawFrame` record, and
four runnable examples (including the mouse-driven ANTIC mode D
paint tool in `04_paint.pas`). The SDK depends on the `Sockets`
and `Classes` / `SysUtils` RTL units only (plus SDL3-for-Pascal
for the paint example), builds on Linux, macOS, Windows, and
FreeBSD, and is typically what you want.

If you're deliberately avoiding the SDK — for example to embed a
tiny client in a ~100-line diagnostic program or to understand
the protocol from first principles — read the Rust example below
and transcribe it: the `uses SysUtils, Classes, Sockets;` setup,
`fpSocket` / `fpConnect` / `fpSend` / `fpRecv`, and `Pos('"ok":true', Resp)`
for the substring check map one-to-one onto `TcpStream`,
`write_all`, `read_line`, and `contains` in the Rust version.

## Minimal client: Go

Single file, stdlib only. Build with `go build altirra_client.go` or
just `go run altirra_client.go <token-file>`.

```go
// altirra_client.go — minimal AltirraBridge client in stdlib Go.
//
// Usage: altirra_client <token-file>

package main

import (
	"bufio"
	"fmt"
	"net"
	"os"
	"strings"
)

func die(format string, args ...any) {
	fmt.Fprintf(os.Stderr, format+"\n", args...)
	os.Exit(1)
}

func main() {
	if len(os.Args) != 2 {
		fmt.Fprintf(os.Stderr, "usage: %s <token-file>\n", os.Args[0])
		os.Exit(2)
	}

	// Token file: two lines — "tcp:HOST:PORT" and a 32-hex token.
	raw, err := os.ReadFile(os.Args[1])
	if err != nil {
		die("read token file: %v", err)
	}
	lines := strings.Split(strings.TrimRight(string(raw), "\n"), "\n")
	if len(lines) < 2 {
		die("token file: expected 2 lines, got %d", len(lines))
	}
	addrLine := strings.TrimSpace(lines[0])
	token := strings.TrimSpace(lines[1])

	addr, ok := strings.CutPrefix(addrLine, "tcp:")
	if !ok {
		die("this example only handles tcp: addresses: %q", addrLine)
	}

	conn, err := net.Dial("tcp", addr)
	if err != nil {
		die("dial: %v", err)
	}
	defer conn.Close()
	reader := bufio.NewReader(conn)

	// One synchronous request/response round-trip.
	rpc := func(cmd string) string {
		if _, err := fmt.Fprintf(conn, "%s\n", cmd); err != nil {
			die("send: %v", err)
		}
		line, err := reader.ReadString('\n')
		if err != nil {
			die("recv: %v", err)
		}
		return strings.TrimRight(line, "\r\n")
	}

	// HELLO is mandatory and must come first.
	hello := rpc("HELLO " + token)
	fmt.Println("HELLO ->", hello)
	if !strings.Contains(hello, `"ok":true`) {
		die("authentication failed")
	}

	fmt.Println("PING  ->", rpc("PING"))
	fmt.Println("FRAME ->", rpc("FRAME 60"))
	fmt.Println("PING  ->", rpc("PING"))
}
```

For a production Go client, wrap `rpc` in a `*Client` struct with
one method per command, swap the substring check for
`encoding/json` unmarshalling into typed response structs, and use
`net.Conn.SetDeadline` to bound each call.

## Running any of the three

```sh
# Terminal 1 — start the headless server:
./AltirraBridgeServer --bridge=tcp:127.0.0.1:0
# Note the token-file path it prints on stderr, e.g.
# /tmp/altirra-bridge-12345.token

# Terminal 2 — run your native client:
./altirra_client /tmp/altirra-bridge-12345.token
```

Expected output (same for all three clients):

```
HELLO -> {"ok":true,"protocol":1,"server":"AltirraSDL","paused":false}
PING  -> {"ok":true}
FRAME -> {"ok":true,"frames":60}
PING  -> {"ok":true}
```

## What to build next

Once a round-trip works, everything else is just more verbs:

- `REGS` / `PEEK addr [len]` / `POKE addr val` — read and write the
  running machine.
- `BOOT path` / `COLD_RESET` / `WARM_RESET` — load software.
- `CONFIG key [value]` — query or set simulator configuration
  (BASIC enable, hardware mode, memory size, break-on-EXE-run).
- `JOY port direction [trigger]` / `KEY name` / `CONSOL …` — inject
  input.
- `SCREENSHOT inline=true` / `RAWSCREEN inline=true` — capture the
  current frame as PNG or raw XRGB8888. The data is base64-encoded
  in the response.
- `DISASM addr [count]` / `HISTORY [count]` / `EVAL expr` /
  `BP_SET …` — the full debugger.

[`COMMANDS.md`](COMMANDS.md) has a one-line summary of every verb;
[`PROTOCOL.md`](PROTOCOL.md) has the complete request/response
shape for each. If you write a client for a language not covered
here and want to share it, drop it in a `sdk/contrib/<lang>/`
directory and open a PR.
