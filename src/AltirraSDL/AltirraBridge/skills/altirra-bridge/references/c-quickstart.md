# C SDK quickstart

The C SDK is a single-file dependency: drop `altirra_bridge.h` +
`altirra_bridge.c` into any project. Dependencies are libc and
Winsock (Windows). No external libraries, no JSON parser required
on the client side — the SDK extracts the few well-known fields
out of responses with simple substring scanning, and exposes the
raw response line via `atb_last_response()` for the rest.

```c
#include "altirra_bridge.h"
```

Link with `ws2_32` on Windows; nothing extra on POSIX.

## Error model

Every function returns an `int`. `ATB_OK` (0) on success, a
negative `ATB_ERR_*` code on failure:

```c
#define ATB_OK             0
#define ATB_ERR_BAD_ARG   -1   // bad argument from caller
#define ATB_ERR_NETWORK   -2   // socket failure
#define ATB_ERR_PROTOCOL  -3   // server returned malformed JSON
#define ATB_ERR_AUTH      -4   // HELLO rejected
#define ATB_ERR_REMOTE    -5   // server returned ok=false
#define ATB_ERR_TOKEN_FILE -6
```

After any failure, `atb_last_error(c)` returns a human-readable
string. After any success, `atb_last_response(c)` returns the
raw JSON line (without trailing newline) — use it to extract
fields the typed wrappers don't expose.

## Connecting

```c
atb_client_t* c = atb_create();
if (!c) abort();

if (atb_connect_token_file(c, "/tmp/altirra-bridge-12345.token") != ATB_OK) {
    fprintf(stderr, "connect failed: %s\n", atb_last_error(c));
    atb_close(c);
    return 1;
}
```

`atb_connect_token_file` reads the file, connects, and performs
HELLO in one call. For more control:

```c
atb_connect(c, "tcp:127.0.0.1:54321");
atb_hello(c, "9ec0...e4");
```

## Lifecycle

```c
atb_ping(c);
atb_pause(c);
atb_resume(c);
atb_frame(c, 60);                /* run 60 frames then re-pause */
atb_quit(c);
```

## Reading state

```c
atb_cpu_state_t s;
atb_regs(c, &s);
printf("PC=%04x A=%02x X=%02x cycles=%lu mode=%s\n",
       s.pc, s.a, s.x, s.cycles, s.mode);

unsigned char buf[64];
atb_peek(c, 0x80, 64, buf);

unsigned int word;
atb_peek16(c, 0xfffc, &word);

unsigned char rgb[768];
atb_palette(c, rgb);
```

The remaining state-read commands (`atb_antic`, `atb_gtia`,
`atb_pokey`, `atb_pia`, `atb_dlist`, `atb_hwstate`) leave the JSON
response in `atb_last_response()` for you to parse with your
preferred JSON library — or with the substring extraction trick
the SDK itself uses internally:

```c
const char* r = atb_last_response(c);
const char* p = strstr(r, "\"PRIOR\":\"");
if (p) {
    p += 9;
    unsigned int prior = (unsigned)strtoul(p + 1, NULL, 16);  /* skip $ */
}
```

## Writing state and injecting input

```c
atb_poke(c, 0x600, 0xab);
atb_poke16(c, 0x602, 0x1234);

unsigned char loader[1024];
atb_memload(c, 0x4000, loader, sizeof loader);

unsigned char dump[256];
atb_memdump(c, 0x4000, sizeof dump, dump);

atb_joy(c, 0, "upright", 1);              /* fire pressed */
atb_key(c, "A", 1, 0);                    /* shift, no ctrl */
atb_consol(c, 1, 0, 0);                   /* hold START */

atb_boot(c, "/path/to/game.xex");
atb_frame(c, 120);

atb_state_save(c, "/tmp/save.altstate");
atb_frame(c, 1);
```

## Rendering

PNG inline:

```c
unsigned char* png; size_t len; unsigned int w, h;
if (atb_screenshot_inline(c, &png, &len, &w, &h) == ATB_OK) {
    FILE* f = fopen("frame.png", "wb");
    fwrite(png, 1, len, f);
    fclose(f);
    free(png);            /* caller owns the buffer */
}
```

PNG to server-side path:

```c
atb_screenshot_path(c, "/tmp/frame.png");
```

Raw XRGB8888 inline (B, G, R, 0 bytes per pixel; stride = w * 4):

```c
unsigned char* px; size_t plen; unsigned int rw, rh;
atb_rawscreen_inline(c, &px, &plen, &rw, &rh);
/* px is exactly rw*rh*4 bytes */
free(px);
```

## Debugger introspection

These commands have variable-shape JSON responses; the wrappers
issue the command and leave the line in `atb_last_response()`.

```c
atb_disasm(c, 0xe477, 8);
atb_history(c, 32);
atb_callstack(c, 8);
atb_memmap(c);
atb_bank_info(c);
atb_cart_info(c);
atb_pmg(c);
atb_audio_state(c);
const char* json = atb_last_response(c);
/* parse with cJSON / jsmn / etc. */
```

The expression evaluator returns its scalar through an out-param:

```c
long val;
atb_eval_expr(c, "dw($fffc)", &val);      /* val = 0xeebb */
```

## Breakpoints

```c
unsigned int id;
atb_bp_set(c, 0xe477, NULL, &id);                    /* unconditional */
atb_bp_set(c, 0x600, "x==0", &id);                   /* conditional */
atb_bp_clear(c, id);
atb_bp_clear_all(c);
atb_bp_list(c);                                      /* JSON in last_response */

/* Watchpoints — mode = "r" | "w" | "rw" (rw creates two BPs) */
atb_watch_set(c, 0xd40b, "rw");
```

## Symbols and search

```c
unsigned int mod, addr;
atb_sym_load(c, "/path/to/game.lab", &mod);
atb_sym_resolve(c, "SIOV", &addr);                   /* addr = 0xe459 */
atb_sym_lookup(c, 0xe459, "rwx");

unsigned char pat[] = { 0x20, 0x59, 0xe4 };          /* JSR $E459 */
atb_memsearch(c, pat, sizeof pat, 0xa000, 0xc000);
```

## Profiler

```c
atb_profile_start(c, "insns");
atb_frame(c, 300);
atb_profile_stop(c);
atb_profile_dump(c, 20);                             /* JSON in last_response */

atb_profile_start(c, "callgraph");
atb_frame(c, 300);
atb_profile_stop(c);
atb_profile_dump_tree(c);
```

`PROFILE_DUMP*` is one-shot per session — calling it twice returns
empty data. The bridge rejects DUMPs while the profiler is still
running; always STOP first.

## Verifier

```c
atb_verifier_set(c, 0x208);                          /* enable */
atb_verifier_status(c);
atb_verifier_set(c, 0);                              /* disable */
```

## Threading

Not thread-safe. One `atb_client_t` per thread. Sharing across
threads will corrupt the per-client receive buffer and the
`last_response`/`last_error` storage.

## Cleanup

```c
atb_close(c);
```

Closes the socket if connected, frees the client. Safe to pass NULL.
Always pair with the matching `atb_create()`.

## A complete minimal example

```c
#include <stdio.h>
#include "altirra_bridge.h"

int main(int argc, char** argv) {
    if (argc != 2) {
        fprintf(stderr, "usage: %s <token-file>\n", argv[0]);
        return 2;
    }

    atb_client_t* c = atb_create();
    if (atb_connect_token_file(c, argv[1]) != ATB_OK) {
        fprintf(stderr, "connect: %s\n", atb_last_error(c));
        atb_close(c);
        return 1;
    }

    atb_boot(c, "/path/to/game.xex");
    atb_frame(c, 120);

    unsigned char* png; size_t len; unsigned int w, h;
    if (atb_screenshot_inline(c, &png, &len, &w, &h) == ATB_OK) {
        FILE* f = fopen("title.png", "wb");
        fwrite(png, 1, len, f);
        fclose(f);
        printf("captured %ux%u (%zu bytes)\n", w, h, len);
        free(png);
    }

    atb_quit(c);
    atb_close(c);
    return 0;
}
```

Compile (Linux/macOS):

```sh
cc -std=c99 -Wall main.c altirra_bridge.c -o demo
```

Compile (Windows MSVC):

```bat
cl /W4 main.c altirra_bridge.c ws2_32.lib
```
