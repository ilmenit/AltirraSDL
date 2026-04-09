/*
 * AltirraBridge C SDK — minimal client for the AltirraBridge protocol.
 *
 * This is a single-file dependency: altirra_bridge.h + altirra_bridge.c.
 * Drop both into any C or C++ project. libc + Winsock are the only
 * dependencies. No external libraries.
 *
 * The protocol is line-delimited UTF-8: send a verb + space-separated
 * arguments terminated by '\n', read back a single-line JSON response
 * terminated by '\n'. The SDK handles transport, framing, and the
 * HELLO authentication handshake. Higher-level command wrappers are
 * added per-phase as the protocol grows.
 *
 * See AltirraBridge/docs/PROTOCOL.md for the full wire contract.
 *
 * Threading: not thread-safe. Use one atb_client_t per thread.
 *
 * Error handling: every function that can fail returns an int with 0
 * for success and a negative atb_err_* code on failure. The most
 * recent error message is available via atb_last_error(client).
 */

#ifndef ALTIRRA_BRIDGE_H
#define ALTIRRA_BRIDGE_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Error codes (negative). */
#define ATB_OK                  0
#define ATB_ERR_BAD_ARG        -1
#define ATB_ERR_NETWORK        -2
#define ATB_ERR_PROTOCOL       -3
#define ATB_ERR_AUTH           -4
#define ATB_ERR_REMOTE         -5  /* server returned ok=false */
#define ATB_ERR_TOKEN_FILE     -6

/* Opaque client handle. Allocate with atb_create(), free with atb_close(). */
typedef struct atb_client atb_client_t;

/*
 * Create an unconnected client. Returns NULL only on memory exhaustion.
 */
atb_client_t* atb_create(void);

/*
 * Close the connection (if any) and free the client. Safe to pass NULL.
 */
void atb_close(atb_client_t* c);

/*
 * Connect to a bridge server. addrSpec accepts the same forms as the
 * server's --bridge argument:
 *   "tcp:127.0.0.1:54321"
 *   "unix:/tmp/altirra-bridge.sock"      (POSIX only)
 *
 * NOTE: This does NOT perform the HELLO handshake. Call atb_hello()
 * next, or use atb_connect_token_file() which does both.
 */
int atb_connect(atb_client_t* c, const char* addrSpec);

/*
 * Send the HELLO handshake with the given session token. The server
 * rejects all other commands until HELLO succeeds.
 */
int atb_hello(atb_client_t* c, const char* token);

/*
 * Convenience: read a token file written by the server (first line is
 * the bound address, second line is the token), connect to that
 * address, and perform the HELLO handshake. This is the one-call
 * "connect to a running AltirraSDL --bridge" entry point.
 *
 * The token file path is logged to stderr by the server on startup,
 * something like "/tmp/altirra-bridge-12345.token".
 */
int atb_connect_token_file(atb_client_t* c, const char* tokenFilePath);

/*
 * Send a raw command (must NOT include a trailing newline; the SDK
 * adds it). Reads back a single-line JSON response. The response
 * pointer is owned by the client and remains valid until the next
 * call on the same client. Returns ATB_OK on transport success even
 * if the server returned {"ok":false,...}; check the response text or
 * use the higher-level wrappers below for cooked semantics.
 */
int atb_send(atb_client_t* c, const char* command, const char** outResponse);

/*
 * Higher-level Phase 1 wrappers. Each returns ATB_OK on a successful
 * {"ok":true,...} response, ATB_ERR_REMOTE on a remote error (with the
 * error message available via atb_last_error()), or a negative
 * transport/protocol error.
 */
int atb_ping(atb_client_t* c);
int atb_pause(atb_client_t* c);
int atb_resume(atb_client_t* c);
int atb_frame(atb_client_t* c, unsigned int n);
int atb_quit(atb_client_t* c);

/*
 * Phase 2 — state read.
 *
 * The C SDK keeps these as thin wrappers over the raw protocol: they
 * send the command, parse a few well-known fields out of the JSON
 * response with simple substring scanning, and expose them as
 * struct/buffer outputs. The raw response remains available via
 * atb_last_response() if the caller needs a field that isn't in the
 * struct.
 *
 * No external JSON parser dependency: the bridge protocol responses
 * are flat enough that strstr/sscanf is sufficient. Higher-level
 * Phase 5+ commands with nested structures (HISTORY, PROFILE_TREE)
 * will ship with a tiny single-file JSON peeker as a sibling.
 */

/* CPU register snapshot. */
typedef struct atb_cpu_state {
    unsigned int pc;       /* 16-bit, possibly 24-bit on 65C816 */
    unsigned int a;
    unsigned int x;
    unsigned int y;
    unsigned int s;
    unsigned int p;        /* status flags packed */
    unsigned long cycles;  /* cycle counter since reset */
    char         flags[9]; /* "NV-BDIZC" with letter set or '-' clear */
    char         mode[8];  /* "6502" / "65C02" / "65C816" */
} atb_cpu_state_t;

int atb_regs(atb_client_t* c, atb_cpu_state_t* out);

/*
 * Read `length` bytes from address `addr` into `out_buf`. Returns
 * ATB_OK on success. Maximum length is 16384 (server-enforced); pass
 * the same length you allocated for `out_buf`. The bytes are written
 * in increasing-address order.
 */
int atb_peek(atb_client_t* c, unsigned int addr, unsigned int length,
             unsigned char* out_buf);

/* Read a 16-bit little-endian word at `addr`. Output via `*out`. */
int atb_peek16(atb_client_t* c, unsigned int addr, unsigned int* out);

/*
 * The remaining state-read commands return the raw JSON response
 * line via atb_last_response() — they don't have struct outputs in
 * the C SDK because the field set is large and varies by chip. Most
 * C clients will either pick fields out of the response with
 * substring scans, or pre-process via the Python SDK and pass
 * structured data to C through some other channel.
 *
 * If a real need arises (likely the bridge::sdl SDK example
 * "render a frame from a state vector"), we can add typed structs
 * for ANTIC/GTIA later. For now, keep the C surface minimal.
 */
int atb_antic(atb_client_t* c);
int atb_gtia(atb_client_t* c);
int atb_pokey(atb_client_t* c);
int atb_pia(atb_client_t* c);
int atb_dlist(atb_client_t* c);
int atb_hwstate(atb_client_t* c);

/*
 * Read the GTIA analysis palette as 256 RGB24 entries. `out_rgb`
 * must point to a buffer of at least 768 bytes (256 * 3). Bytes are
 * R0, G0, B0, R1, G1, B1, ...
 */
int atb_palette(atb_client_t* c, unsigned char out_rgb[768]);

/*
 * Upload a 768-byte Adobe Color Table (.act) and run the same
 * palette-fitting solver that Windows Altirra uses in its Color
 * Image Reference dialog. Two passes, matching=None then
 * matching=sRGB. The server updates the active profile's NTSC (or
 * PAL) analog decoder parameters so GTIA composites subsequent
 * frames through a palette that approximates the supplied .act.
 *
 * `act_bytes` must point to exactly 768 bytes (256 * RGB). On
 * success returns ATB_OK and, if `out_rms_error` is non-NULL,
 * writes the solver's final per-channel standard-error there
 * (same metric the Windows dialog reports).
 *
 * Runs synchronously on the server's main thread — typically
 * 50-200 ms wall-clock depending on the .act and CPU. Persists
 * until atb_palette_reset() or atb_cold_reset() is called.
 */
int atb_palette_load_act(atb_client_t* c,
                         const unsigned char act_bytes[768],
                         float* out_rms_error);

/*
 * Restore GTIA's factory-default NTSC and PAL color parameters,
 * undoing any prior atb_palette_load_act(). Returns ATB_OK on
 * success.
 */
int atb_palette_reset(atb_client_t* c);

/*
 * Phase 3 — state write & input injection.
 *
 * The C SDK keeps these as thin command builders. They send a
 * formatted line to the server and parse the {"ok":true} response.
 * For commands that return useful payload (none in Phase 3 beyond
 * acknowledgement), inspect atb_last_response().
 */

int atb_poke(atb_client_t* c, unsigned int addr, unsigned int value);
int atb_poke16(atb_client_t* c, unsigned int addr, unsigned int value);

/*
 * Hardware-register poke. Unlike atb_poke(), which writes the
 * debug-safe RAM latch and has no side effects on ANTIC / GTIA /
 * POKEY / PIA registers, atb_hwpoke() routes the write through
 * the real CPU bus, triggering the same chip write handlers a
 * `STA $Dxxx` instruction would on the 6502. Use this to drive
 * ANTIC's DLIST / DMACTL / NMIEN, GTIA's colour registers, etc.
 * from a bare-metal client that has parked the CPU via
 * atb_boot_bare().
 */
int atb_hwpoke(atb_client_t* c, unsigned int addr, unsigned int value);

/*
 * Write `length` bytes from `data` into RAM at `addr`. The bytes
 * are sent inline as base64; works over `adb forward` on Android
 * with no shared filesystem. Maximum length depends on the server-
 * side recv buffer cap (1 MB).
 */
int atb_memload(atb_client_t* c, unsigned int addr,
                const unsigned char* data, unsigned int length);

/*
 * Read `length` bytes (max 65536) starting at `addr` into `out_buf`.
 */
int atb_memdump(atb_client_t* c, unsigned int addr, unsigned int length,
                unsigned char* out_buf);

/*
 * Joystick input. `port` is 0..3 (port 0 = joystick 1, port 1 =
 * joystick 2, etc.). `direction` is one of "centre", "up", "down",
 * "left", "right", "upleft", "upright", "downleft", "downright"
 * (compass forms also accepted: "n", "ne", "e", "se", "s", "sw",
 * "w", "nw", "c"). `fire` non-zero presses the trigger button.
 *
 * The bridge owns its own input slot; injected joystick state does
 * NOT fight with real-gamepad input from the host. Disconnect
 * cleanup automatically releases everything.
 */
int atb_joy(atb_client_t* c, unsigned int port,
            const char* direction, int fire);

/*
 * Push one keystroke into POKEY's queue. `name` is a key
 * identifier ("A", "SPACE", "RETURN", "ESC", "1", ...). Letters
 * are case-insensitive. `shift`/`ctrl` non-zero set the
 * corresponding KBCODE modifier bits.
 */
int atb_key(atb_client_t* c, const char* name, int shift, int ctrl);

/*
 * Set console switch state (active-low). Each non-zero argument
 * holds the corresponding switch down; zero releases it. Pass all
 * zeros to release everything.
 */
int atb_consol(atb_client_t* c, int start, int select, int option);

/*
 * Boot a media file (XEX, ATR, CAS, CAR). The action is queued
 * via the SDL3 deferred-action system and processed at the start
 * of the next main-loop frame. To wait for the boot to complete
 * before reading state, call atb_frame(c, N) afterwards.
 */
int atb_boot(atb_client_t* c, const char* path);
int atb_mount(atb_client_t* c, unsigned int drive, const char* path);

/*
 * Boot the embedded "bare-metal" stub: a 30-byte XEX shipped inside
 * the server that disables IRQs, NMIs, BASIC, and ANTIC DMA and then
 * parks the CPU in an infinite JMP * loop. After this returns, the
 * client owns the machine and can POKE ANTIC $D402/$D403 directly,
 * install a display list anywhere in RAM, enable DMACTL, and write
 * pixel data without any OS code modifying state from under it.
 *
 * settle_frames: number of frames to advance after the BOOT_BARE
 * command so the OS loader has time to jump to the stub and the
 * stub has time to execute (SEI/CLD/STA/JMP*). Pass 180 for a
 * reliable settle (covers both NTSC and PAL OS boot + stub run).
 * Pass 0 to take the machine immediately without waiting.
 *
 * See sdk/c/examples/04_paint.c for the canonical use case.
 */
int atb_boot_bare(atb_client_t* c, unsigned int settle_frames);

int atb_cold_reset(atb_client_t* c);
int atb_warm_reset(atb_client_t* c);

int atb_state_save(atb_client_t* c, const char* path);
int atb_state_load(atb_client_t* c, const char* path);

/*
 * Phase 4 — rendering.
 *
 * atb_screenshot_inline() allocates *out_png via malloc() and
 * returns the PNG byte count via *out_len. Caller owns the buffer
 * and must free() it. Returns ATB_OK on success.
 *
 * atb_screenshot_path() writes the PNG to a file on the server-
 * side filesystem (useful when client and server share a disk).
 *
 * atb_rawscreen_inline() returns the raw XRGB8888 little-endian
 * buffer plus its dimensions. Pixel format: each 4-byte group is
 * (B, G, R, 0) in memory order. stride == width * 4. Caller owns
 * *out_pixels and must free() it.
 */
int atb_screenshot_inline(atb_client_t* c,
                          unsigned char** out_png, size_t* out_len,
                          unsigned int* out_w, unsigned int* out_h);
int atb_screenshot_path(atb_client_t* c, const char* path);

int atb_rawscreen_inline(atb_client_t* c,
                         unsigned char** out_pixels, size_t* out_len,
                         unsigned int* out_w, unsigned int* out_h);

/*
 * Phase 5a — debugger introspection.
 *
 * These commands have variable-shape JSON payloads. The C SDK
 * sends the command and leaves the response line in
 * atb_last_response() for the caller to parse. The scalar helpers
 * (atb_eval_expr) return the key datum directly.
 */
int atb_disasm(atb_client_t* c, unsigned int addr, unsigned int count);
int atb_history(atb_client_t* c, unsigned int count);
int atb_eval_expr(atb_client_t* c, const char* expr, long* out_value);
int atb_callstack(atb_client_t* c, unsigned int count);
int atb_memmap(atb_client_t* c);
int atb_bank_info(atb_client_t* c);
int atb_cart_info(atb_client_t* c);
int atb_pmg(atb_client_t* c);
int atb_audio_state(atb_client_t* c);

/*
 * Phase 5b — breakpoints, symbols, memsearch, profiler, verifier.
 */

int atb_bp_set(atb_client_t* c, unsigned int addr,
               const char* condition /* NULL for unconditional */,
               unsigned int* out_id);
int atb_bp_clear(atb_client_t* c, unsigned int id);
int atb_bp_clear_all(atb_client_t* c);
int atb_bp_list(atb_client_t* c);

/* mode: "r" | "w" | "rw" — rw creates two breakpoints and returns
 * both ids in the JSON response. */
int atb_watch_set(atb_client_t* c, unsigned int addr, const char* mode);

int atb_sym_load(atb_client_t* c, const char* path, unsigned int* out_module_id);
int atb_sym_resolve(atb_client_t* c, const char* name, unsigned int* out_addr);
int atb_sym_lookup(atb_client_t* c, unsigned int addr, const char* flags);

/*
 * Linear memory search. `pattern` / `pattern_len` are raw bytes;
 * the SDK hex-encodes them on the wire. `start`/`end` bound the
 * search window (0..0x10000).
 */
int atb_memsearch(atb_client_t* c, const unsigned char* pattern,
                  size_t pattern_len,
                  unsigned int start, unsigned int end);

/* mode: "insns" | "functions" | "callgraph" | "basicblock" */
int atb_profile_start(atb_client_t* c, const char* mode);
int atb_profile_stop(atb_client_t* c);
int atb_profile_status(atb_client_t* c);
int atb_profile_dump(atb_client_t* c, unsigned int top);
int atb_profile_dump_tree(atb_client_t* c);

/* Set verifier flag bitmask. Pass 0 to disable. */
int atb_verifier_set(atb_client_t* c, unsigned int flags);
int atb_verifier_status(atb_client_t* c);

/*
 * Most recent error message (server-side or client-side). Never NULL;
 * empty string when no error. Owned by the client.
 */
const char* atb_last_error(const atb_client_t* c);

/*
 * Most recent raw response line, including the leading "{" but
 * without the trailing newline. Owned by the client; valid until the
 * next call.
 */
const char* atb_last_response(const atb_client_t* c);

#ifdef __cplusplus
}  /* extern "C" */
#endif

#endif  /* ALTIRRA_BRIDGE_H */
