# AltirraBridge Command Reference

Quick-reference for every wire command, with Python and C SDK
examples. For full request/response schemas and field semantics see
[`PROTOCOL.md`](PROTOCOL.md).

| Group        | Commands |
|--------------|----------|
| [Lifecycle](#lifecycle)               | `HELLO`, `PING`, `PAUSE`, `RESUME`, `FRAME`, `QUIT` |
| [State read](#state-read)             | `REGS`, `PEEK`, `PEEK16`, `ANTIC`, `GTIA`, `POKEY`, `PIA`, `DLIST`, `HWSTATE`, `PALETTE` |
| [State write & input](#state-write--input) | `POKE`, `POKE16`, `MEMDUMP`, `MEMLOAD`, `JOY`, `KEY`, `CONSOL`, `BOOT`, `MOUNT`, `COLD_RESET`, `WARM_RESET`, `STATE_SAVE`, `STATE_LOAD` |
| [Rendering](#rendering)               | `SCREENSHOT`, `RAWSCREEN`, `RENDER_FRAME` |
| [Debugger introspection](#debugger-introspection) | `DISASM`, `HISTORY`, `EVAL`, `CALLSTACK`, `MEMMAP`, `BANK_INFO`, `CART_INFO`, `PMG`, `AUDIO_STATE` |
| [Breakpoints](#breakpoints)           | `BP_SET`, `BP_CLEAR`, `BP_CLEAR_ALL`, `BP_LIST`, `WATCH_SET` |
| [Symbols & search](#symbols--search)  | `SYM_LOAD`, `SYM_RESOLVE`, `SYM_LOOKUP`, `MEMSEARCH` |
| [Profiler](#profiler)                 | `PROFILE_START`, `PROFILE_STOP`, `PROFILE_STATUS`, `PROFILE_DUMP`, `PROFILE_DUMP_TREE` |
| [Verifier](#verifier)                 | `VERIFIER_STATUS`, `VERIFIER_SET` |

---

## Lifecycle

```python
from altirra_bridge import AltirraBridge

with AltirraBridge.from_token_file("/tmp/altirra-bridge-12345.token") as a:
    a.ping()           # liveness
    a.frame(60)        # run 60 frames then re-pause
    a.pause()
    a.resume()
    a.quit()           # tells the server to exit
```

```c
#include "altirra_bridge.h"
atb_client_t* c = atb_create();
atb_connect_token_file(c, "/tmp/altirra-bridge-12345.token");
atb_ping(c);
atb_frame(c, 60);
atb_quit(c);
atb_close(c);
```

## State read

```python
regs   = a.regs()                       # {'PC':'$e477', 'A':'$ff', ..., 'cycles':...}
data   = a.peek(0x600, 64)              # bytes
word   = a.peek16(0xfffc)               # int (reset vector)
hw     = a.hwstate()                    # CPU + ANTIC + GTIA + POKEY + PIA in one trip
pal    = a.palette()                    # 768-byte RGB palette
dlist  = a.dlist()                      # decoded display list entries
```

```c
atb_cpu_state_t s; atb_regs(c, &s);
unsigned char buf[64]; atb_peek(c, 0x600, 64, buf);
unsigned int w; atb_peek16(c, 0xfffc, &w);
unsigned char rgb[768]; atb_palette(c, rgb);
```

## State write & input

```python
a.poke(0x80, 0xab)
a.memload(0x4000, open("loader.bin", "rb").read())
data = a.memdump(0x4000, 0x100)
a.joy(0, "upright", fire=True)
a.key("A", shift=True)                  # types capital A
a.consol(start=True)
a.boot("/path/to/game.xex"); a.frame(120)   # wait for boot
a.state_save("/tmp/session.altstate"); a.frame(1)
```

```c
atb_poke(c, 0x80, 0xab);
atb_memload(c, 0x4000, loader_bytes, loader_len);
unsigned char dump[256]; atb_memdump(c, 0x4000, 256, dump);
atb_joy(c, 0, "upright", 1);
atb_key(c, "A", 1, 0);
atb_consol(c, 1, 0, 0);
atb_boot(c, "/path/to/game.xex"); atb_frame(c, 120);
```

## Rendering

```python
# Inline PNG (works over adb forward, no shared filesystem)
png_bytes = a.screenshot()
open("frame.png", "wb").write(png_bytes)

# Server-side write
a.screenshot(path="/tmp/frame.png")

# Raw XRGB8888 (B,G,R,0 byte order on the wire)
frame = a.rawscreen()
print(frame.width, frame.height, len(frame.pixels))

# For PIL: Image.frombytes("RGBA", (w, h), frame.pixels_rgba())
```

```c
unsigned char* png; size_t len; unsigned int w, h;
atb_screenshot_inline(c, &png, &len, &w, &h);
fwrite(png, 1, len, fp); free(png);

atb_screenshot_path(c, "/tmp/frame.png");

unsigned char* rgba; size_t rlen; unsigned int rw, rh;
atb_rawscreen_inline(c, &rgba, &rlen, &rw, &rh);
free(rgba);
```

## Debugger introspection

```python
# Disassemble at the reset vector
for ins in a.disasm(0xe477, count=8):
    print(ins['addr'], ins['text'])

# Last 32 instructions executed
for h in a.history(32):
    print(f"{h['cycle']:>10}  {h['pc']}  op={h['op']}  a={h['a']}")

# Expression evaluation (lowercase register names)
print(a.eval_expr("dw($fffc)"))         # reset vector value
print(a.eval_expr("a + x * 2"))

# Call stack
for f in a.callstack(8):
    print(f['pc'])

# Memory layout & banking
for region in a.memmap():
    print(region['name'], region['lo'], '-', region['hi'], region['kind'])
print(a.bank_info())
print(a.cart_info())

# Player/missile and POKEY decoded state
print(a.pmg())
print(a.audio_state())
```

```c
atb_disasm(c, 0xe477, 8);
const char* json = atb_last_response(c);  /* parse with your favourite JSON lib */

long val;
atb_eval_expr(c, "dw($fffc)", &val);

atb_callstack(c, 8);
atb_memmap(c);
atb_pmg(c);
atb_audio_state(c);
```

## Breakpoints

```python
bp = a.bp_set(0xe477)
bp_cond = a.bp_set(0x600, condition="x==0")

print(a.bp_list())                          # list every BP

a.bp_clear(bp)
a.bp_clear_all()

# Read watchpoint at RANDOM
ids = a.watch_set(0xd40b, mode="r")

# Read+write watch creates two breakpoints; rolls back on partial failure
ids_rw = a.watch_set(0xd40b, mode="rw")
```

```c
unsigned int bp_id;
atb_bp_set(c, 0xe477, NULL, &bp_id);
atb_bp_set(c, 0x600, "x==0", &bp_id);
atb_bp_clear(c, bp_id);

atb_watch_set(c, 0xd40b, "rw");
```

## Symbols & search

```python
mod_id = a.sym_load("/path/to/game.lab")
addr   = a.sym_resolve("SIOV")              # → $e459
sym    = a.sym_lookup(0xe459)               # {'name':'SIOV','base':'$e459','offset':0}

# Find every JSR to $e459 (4c d8 ee for JMP, 20 d8 ee for JSR)
hits   = a.memsearch(b"\x20\x59\xe4", start=0xa000, end=0xc000)
```

```c
unsigned int mod, addr;
atb_sym_load(c, "/path/to/game.lab", &mod);
atb_sym_resolve(c, "SIOV", &addr);
atb_sym_lookup(c, 0xe459, "rwx");

unsigned char pat[] = { 0x20, 0x59, 0xe4 };
atb_memsearch(c, pat, sizeof pat, 0xa000, 0xc000);
```

## Profiler

```python
a.profile_start(mode="insns")
a.frame(300)                                # collect data
a.profile_stop()
report = a.profile_dump(top=20)
for h in report['hot']:
    print(f"{h['addr']}  cycles={h['cycles']:>8}  calls={h['calls']}")

# Hierarchical call tree
a.profile_start(mode="callgraph")
a.frame(300); a.profile_stop()
for node in a.profile_dump_tree():
    print(node['addr'], 'parent=', node['parent'],
          'incl_cycles=', node['incl_cycles'])
```

```c
atb_profile_start(c, "insns");
atb_frame(c, 300);
atb_profile_stop(c);
atb_profile_dump(c, 20);
```

**Note:** `PROFILE_DUMP*` is destructive — calling it twice returns
empty data the second time. Restart the profiler to collect a new
session.

## Verifier

```python
# Enable StackWrap (0x200) + RecursiveNMI (0x02)
a.verifier_set(0x202)
print(a.verifier_status())                  # {'enabled': True, 'flags': '$202'}
a.verifier_set(0)                           # disable
```

```c
atb_verifier_set(c, 0x202);
atb_verifier_status(c);
atb_verifier_set(c, 0);
```

Verifier violations are reported through Altirra's debugger console
output. v1 of the bridge does **not** expose a structured violation
log; this is a roadmap item awaiting a public log-sink API in
Altirra core.

---

For the wire-level details (exact JSON shapes, error formats,
versioning rules, threat model), see [`PROTOCOL.md`](PROTOCOL.md).
For getting-started, see [`GETTING_STARTED.md`](GETTING_STARTED.md).
