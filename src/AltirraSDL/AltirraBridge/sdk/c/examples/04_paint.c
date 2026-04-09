/*
 * 04_paint.c — "emulator as raw display device" example.
 *
 * Mouse-driven paint program that drives Altirra directly as a
 * 160x96 4-colour ANTIC mode D framebuffer. The left half of the
 * window is a local paint canvas; the right half shows the
 * corresponding live Atari frame, rendered by the real Altirra
 * emulator through the bridge.
 *
 * What this example shows:
 *
 *   1. atb_boot_bare() — boots a 30-byte stub that disables every
 *      piece of the OS that would fight a client trying to draw
 *      (IRQs, NMIs / VBI / DLI, BASIC cart, ANTIC DMA) and then
 *      parks the CPU in an infinite JMP * loop. After this runs
 *      the client owns the machine entirely.
 *
 *   2. atb_palette_load_act() — loads an Adobe Color Table (.act)
 *      file and runs Altirra's palette-fitting solver so GTIA
 *      composites subsequent frames through the supplied palette.
 *      The .act ships next to the example binary as g2f.act (the
 *      palette Graph2Font uses by default).
 *
 *   3. Direct ANTIC / GTIA register writes — with the CPU parked
 *      and no OS VBI running, the client pokes $D402/$D403 to
 *      install a display list, $D400 to enable ANTIC DMA, and
 *      $D016-$D01A for the playfield colour registers. None of
 *      these writes are ever fought with by the running kernel.
 *
 *   4. Mode D pixel packing — 4 pixels per byte at 2 bpp MSB-first.
 *
 *   5. Bulk shadow -> server upload via atb_memload (one round
 *      trip per paint batch, not per pixel).
 *
 *   6. Reading the live frame back via atb_rawscreen_inline and
 *      blitting it to an SDL3 texture.
 *
 * The C SDK itself is libc-only by design. THIS EXAMPLE adds an
 * SDL3 dependency for the window and event loop — drop in any
 * other UI library by replacing the event-loop + render section.
 *
 * Controls:
 *   click swatch / 1,2,3   select paint colour
 *   click X      / C       clear canvas
 *   ESC or close window    quit
 *
 * Build (with SDL3 installed system-wide):
 *   cd sdk/c/examples
 *   mkdir build && cd build
 *   cmake .. -DCMAKE_BUILD_TYPE=Release
 *   cmake --build .
 *
 * Run:
 *   ./04_paint /tmp/altirra-bridge-<pid>.token
 *
 * The example looks for g2f.act in the following locations (in
 * order), skipping the .act load if none is found:
 *   1. $ALTIRRA_BRIDGE_ACT environment variable (absolute path)
 *   2. g2f.act next to the executable
 *   3. ../assets/g2f.act (when run from sdk/c/examples/bin/)
 *   4. ../../assets/g2f.act (when run from build trees)
 */

#include "altirra_bridge.h"

#include <SDL3/SDL.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* =====================================================================
 * Atari layout (must match 04_paint.py byte-for-byte)
 * ===================================================================== */

#define DL_ADDR     0x1000
#define SCREEN_ADDR 0x2000

/* ANTIC mode D = "Graphics 7": 160 x 96 pixels, 4 colors, 2 bpp,
 * 40 bytes/row, each row is 2 scanlines tall. */
#define W           160
#define H           96
#define ROW_BYTES   40
#define SCREEN_LEN  (ROW_BYTES * H)    /* 3840 bytes */

/* GTIA / ANTIC hardware registers. Because we boot the bare-metal
 * stub before touching any of these, no VBI / DLI / OS code ever
 * runs after the stub parks the CPU, so we can write the hardware
 * registers directly and they stay written until we overwrite
 * them ourselves. This is the whole point of the bare-metal boot
 * model; see docs/EMULATOR_AS_DEVICE.md for the three cases. */
#define ANTIC_DMACTL  0xD400    /* playfield DMA enable / width    */
#define ANTIC_DLISTL  0xD402    /* display list address low        */
#define ANTIC_DLISTH  0xD403    /* display list address high       */
#define GTIA_COLPF0   0xD016    /* mode D pixel value 1            */
#define GTIA_COLPF1   0xD017    /* mode D pixel value 2            */
#define GTIA_COLPF2   0xD018    /* mode D pixel value 3            */
#define GTIA_COLBK    0xD01A    /* mode D pixel value 0 (background)*/

/* Atari color values (hue << 4 | luma) — same palette as the
 * Python version: white, red, green, on a black background. */
static const unsigned char ATARI_PALETTE[] = { 0x0F, 0x46, 0xC8 };

/* Crude RGB approximations for the on-screen drawing canvas.
 * The right-side preview uses the real Altirra palette. */
static const unsigned int CANVAS_RGB[4] = {
    0x000000,    /* 0 = background */
    0xFFFFFF,    /* 1 = white */
    0xC04040,    /* 2 = red   */
    0x40C080,    /* 3 = green */
};

/* =====================================================================
 * Display list construction + bridge setup (identical to 04_paint.py)
 * ===================================================================== */

static size_t build_display_list(unsigned char* dl) {
    size_t n = 0;
    dl[n++] = 0x70; dl[n++] = 0x70; dl[n++] = 0x70;          /* 24 blank scanlines */
    dl[n++] = 0x4D;                                           /* mode D + LMS */
    dl[n++] = SCREEN_ADDR & 0xFF;
    dl[n++] = SCREEN_ADDR >> 8;
    for (int i = 0; i < H - 1; ++i) dl[n++] = 0x0D;           /* 95 more rows */
    dl[n++] = 0x41;                                           /* JVB */
    dl[n++] = DL_ADDR & 0xFF;
    dl[n++] = DL_ADDR >> 8;
    return n;
}

/* Take full control of the machine and install the custom mode D
 * display list. Sequence:
 *
 *   1. BOOT_BARE       — the server loads its embedded 30-byte
 *                        stub .xex, which disables IRQs, NMIs,
 *                        BASIC, and ANTIC DMA, then parks the CPU
 *                        in JMP *. Settle 180 frames so the stub
 *                        has actually executed.
 *
 *   2. memload the DL  — push the mode D DL to $1000.
 *
 *   3. memload pixels  — clear the 3840-byte screen buffer at
 *                        $2000 so the canvas starts black.
 *
 *   4. Write ANTIC     — $D402/$D403 point ANTIC at our DL;
 *                        $D400 = $22 enables playfield DMA at
 *                        normal width, single-line resolution.
 *
 *   5. Write GTIA      — playfield colour registers for the four
 *                        mode D pixel values. Because the CPU is
 *                        parked no VBI will ever copy the OS
 *                        shadows ($02C4-$02C8) to these registers,
 *                        so we write the hardware directly.
 *
 * After this returns, any subsequent memload to $2000..$2EFF
 * shows up in the Atari frame one emulator frame later. */
static int setup_machine(atb_client_t* c) {
    /* settle_frames=0: the server's CmdBootBare now waits
     * synchronously for the stub to finish loading and the CPU
     * to park, so we don't need a client-side frame handshake. */
    if (atb_boot_bare(c, 0) != ATB_OK) return -1;

    unsigned char dl[128];
    size_t dl_len = build_display_list(dl);
    unsigned char clear[SCREEN_LEN] = {0};

    if (atb_memload(c, DL_ADDR,     dl,    (unsigned int)dl_len) != ATB_OK) return -1;
    if (atb_memload(c, SCREEN_ADDR, clear, SCREEN_LEN)           != ATB_OK) return -1;

    /* Point ANTIC at the custom DL. Uses HWPOKE (not POKE) because
     * the debug-safe POKE path bypasses I/O register write
     * handlers — writing to the RAM latch at $D402 has no effect
     * on ANTIC's real DLISTL/DLISTH. HWPOKE goes through the same
     * CPU bus path as a `STA $D402` instruction would. */
    if (atb_hwpoke(c, ANTIC_DLISTL, DL_ADDR & 0xFF) != ATB_OK) return -1;
    if (atb_hwpoke(c, ANTIC_DLISTH, DL_ADDR >> 8)   != ATB_OK) return -1;

    /* Playfield colour registers. */
    if (atb_hwpoke(c, GTIA_COLBK,  0x00)             != ATB_OK) return -1;
    if (atb_hwpoke(c, GTIA_COLPF0, ATARI_PALETTE[0]) != ATB_OK) return -1;
    if (atb_hwpoke(c, GTIA_COLPF1, ATARI_PALETTE[1]) != ATB_OK) return -1;
    if (atb_hwpoke(c, GTIA_COLPF2, ATARI_PALETTE[2]) != ATB_OK) return -1;

    /* Finally enable ANTIC DMA: $22 = playfield on, normal width,
     * single-line DMA. Written last so ANTIC starts fetching from
     * the right DL address on the very next scan. */
    if (atb_hwpoke(c, ANTIC_DMACTL, 0x22) != ATB_OK) return -1;
    return 0;
}

/* Load a .act palette file from the filesystem and send it to
 * the bridge server. The server runs the same palette-fitting
 * solver Windows Altirra uses in its Color Image Reference dialog
 * and updates GTIA's active colour parameters in place. Returns
 * 0 on success, -1 on file / network error, -2 if no file could
 * be found (not treated as fatal by the caller). */
static int try_load_act(atb_client_t* c, const char* argv0) {
    /* 1. Environment override. */
    const char* envPath = getenv("ALTIRRA_BRIDGE_ACT");

    /* 2. Path relative to the executable. Derive the exe dir by
     *    trimming the last path component of argv[0]. This is a
     *    best-effort heuristic — if argv[0] is just "04_paint"
     *    (no slash), we fall through to the other candidates. */
    char exeDir[1024] = {0};
    if (argv0) {
        const char* slash = strrchr(argv0, '/');
#if defined(_WIN32)
        const char* bslash = strrchr(argv0, '\\');
        if (bslash && (!slash || bslash > slash)) slash = bslash;
#endif
        if (slash) {
            size_t n = (size_t)(slash - argv0);
            if (n >= sizeof exeDir) n = sizeof exeDir - 1;
            memcpy(exeDir, argv0, n);
            exeDir[n] = '\0';
        }
    }

    const char* candidates[8];
    char buf1[1200], buf2[1200], buf3[1200], buf4[1200];
    int n = 0;
    if (envPath && *envPath) candidates[n++] = envPath;
    if (exeDir[0]) {
        snprintf(buf1, sizeof buf1, "%s/g2f.act",            exeDir);
        snprintf(buf2, sizeof buf2, "%s/../assets/g2f.act",  exeDir);
        snprintf(buf3, sizeof buf3, "%s/../../assets/g2f.act", exeDir);
        candidates[n++] = buf1;
        candidates[n++] = buf2;
        candidates[n++] = buf3;
    }
    /* Fallback: current working directory (for manual runs). */
    snprintf(buf4, sizeof buf4, "g2f.act");
    candidates[n++] = buf4;

    FILE* f = NULL;
    const char* chosen = NULL;
    for (int i = 0; i < n; ++i) {
        f = fopen(candidates[i], "rb");
        if (f) { chosen = candidates[i]; break; }
    }
    if (!f) {
        fprintf(stderr,
            "note: no g2f.act palette file found — continuing with the\n"
            "      server's default NTSC palette. Set ALTIRRA_BRIDGE_ACT\n"
            "      or place g2f.act next to the binary to enable it.\n");
        return -2;
    }
    unsigned char act[768];
    size_t got = fread(act, 1, sizeof act, f);
    fclose(f);
    if (got != 768) {
        fprintf(stderr, "error: %s is %zu bytes, expected 768\n", chosen, got);
        return -1;
    }
    float err = 0.0f;
    if (atb_palette_load_act(c, act, &err) != ATB_OK) {
        fprintf(stderr, "PALETTE_LOAD_ACT failed: %s\n", atb_last_error(c));
        return -1;
    }
    fprintf(stderr, "loaded .act palette from %s (solver RMS error %.2f)\n",
            chosen, err);
    return 0;
}

/* =====================================================================
 * Mode D pixel pack
 *
 *     byte = | px0 (b7-6) | px1 (b5-4) | px2 (b3-2) | px3 (b1-0) |
 * ===================================================================== */

static void pack_pixel(unsigned char* buf, int x, int y, int color) {
    if (x < 0 || x >= W || y < 0 || y >= H) return;
    if (color < 0 || color > 3)              return;
    int offset = y * ROW_BYTES + (x >> 2);
    int shift  = (3 - (x & 3)) * 2;
    int mask   = 0x03 << shift;
    buf[offset] = (unsigned char)((buf[offset] & ~mask) | (color << shift));
}

static int read_pixel(const unsigned char* buf, int x, int y) {
    int offset = y * ROW_BYTES + (x >> 2);
    int shift  = (3 - (x & 3)) * 2;
    return (buf[offset] >> shift) & 0x03;
}

/* =====================================================================
 * Window layout
 * ===================================================================== */

#define SCALE         4                 /* drawing canvas scale factor */
#define CANVAS_W      (W * SCALE)       /* 640 */
#define CANVAS_H      (H * SCALE)       /* 384 */
/* Reserve a generous slot on the right for the live Atari frame.
 * The actual frame size reported by RAWSCREEN depends on the server's
 * overscan/region settings (NTSC OS screen is 336x240 but Extended /
 * Full / PAL produce different dimensions). The example adapts at
 * runtime — it (re)creates the texture on the first frame and on any
 * subsequent size change, instead of silently dropping frames that
 * don't match a hard-coded constant. */
#define ATARI_SLOT_W  456               /* room for Full NTSC overscan */
#define ATARI_SLOT_H  312               /* room for Full PAL  overscan */
#define GUTTER        8
#define WINDOW_W      (CANVAS_W + GUTTER + ATARI_SLOT_W)
/* Leave room under the Atari slot for the toolbar (swatches + clear). */
#define TOOLBAR_H     48
#define WINDOW_H      (((CANVAS_H) > (ATARI_SLOT_H + TOOLBAR_H)) \
                       ? (CANVAS_H) : (ATARI_SLOT_H + TOOLBAR_H))

/* Toolbar widget rects (logical coords — shared by the hit-test in
 * the event loop and the draw code in the render pass). */
#define TOOLBAR_Y     (ATARI_SLOT_H + 8)
#define SWATCH_W      28
#define SWATCH_H      28
#define SWATCH_STRIDE 34
#define SWATCH_X(i)   ((float)(CANVAS_W + GUTTER + (i) * SWATCH_STRIDE))
#define CLEAR_X       ((float)(CANVAS_W + GUTTER + 3 * SWATCH_STRIDE + 16))
#define CLEAR_W       72
#define CLEAR_H       28

static int in_rect(float px, float py, float rx, float ry, float rw, float rh) {
    return px >= rx && px < rx + rw && py >= ry && py < ry + rh;
}

/* Build a 160 x 96 XRGB8888 image of the local shadow buffer. The
 * canvas texture is 160x96 logical, scaled up by the renderer. */
static void shadow_to_canvas_texture(const unsigned char* shadow,
                                     unsigned int* out /* W*H */) {
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            int c = read_pixel(shadow, x, y);
            out[y * W + x] = CANVAS_RGB[c];
        }
    }
}

/* =====================================================================
 * main
 * ===================================================================== */

int main(int argc, char** argv) {
    if (argc != 2) {
        fprintf(stderr, "usage: %s <token-file>\n", argv[0]);
        fprintf(stderr,
            "  The token file path is logged on stderr by\n"
            "  AltirraBridgeServer or AltirraSDL --bridge.\n");
        return 2;
    }

    /* --- Bridge --- */
    atb_client_t* c = atb_create();
    if (!c) { fprintf(stderr, "out of memory\n"); return 1; }

    if (atb_connect_token_file(c, argv[1]) != ATB_OK) {
        fprintf(stderr, "connect/hello failed: %s\n", atb_last_error(c));
        atb_close(c);
        return 1;
    }

    if (setup_machine(c) != 0) {
        fprintf(stderr, "setup failed: %s\n", atb_last_error(c));
        atb_close(c);
        return 1;
    }

    /* Optional .act palette. Failure to find / load is logged but
     * not fatal; the example still works with the default NTSC
     * palette, just with slightly different on-screen colours. */
    (void)try_load_act(c, argv[0]);

    /* --- SDL3 --- */
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        atb_close(c);
        return 1;
    }

    SDL_Window*   window  = SDL_CreateWindow(
        "AltirraBridge paint demo  "
        "[click swatch or 1/2/3 = colour   click X or C = clear   ESC = quit]",
        WINDOW_W, WINDOW_H, 0);
    if (!window) {
        fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError());
        SDL_Quit(); atb_close(c); return 1;
    }
    SDL_Renderer* renderer = SDL_CreateRenderer(window, NULL);
    if (!renderer) {
        fprintf(stderr, "SDL_CreateRenderer: %s\n", SDL_GetError());
        SDL_DestroyWindow(window); SDL_Quit(); atb_close(c); return 1;
    }
    SDL_SetRenderLogicalPresentation(renderer, WINDOW_W, WINDOW_H,
                                     SDL_LOGICAL_PRESENTATION_LETTERBOX);
    /* This is a tool window, not a game — don't wait for the display's
     * vblank between present calls. The 16 ms vsync wait used to
     * dominate the paint-to-visible latency. */
    SDL_SetRenderVSync(renderer, 0);

    /* Drawing canvas: 160 x 96 logical, scaled by renderer */
    SDL_Texture* canvas_tex = SDL_CreateTexture(
        renderer, SDL_PIXELFORMAT_XRGB8888,
        SDL_TEXTUREACCESS_STREAMING, W, H);
    SDL_SetTextureScaleMode(canvas_tex, SDL_SCALEMODE_NEAREST);

    /* Live Atari frame: format matches the bridge's RAWSCREEN
     * payload (XRGB8888 little-endian — byte order BGRX on the
     * wire). Dimensions are not known until we see the first
     * frame; the texture is created / recreated lazily in the
     * main loop whenever the reported size changes. */
    SDL_Texture* atari_tex = NULL;
    int          atari_tex_w = 0;
    int          atari_tex_h = 0;

    if (!canvas_tex) {
        fprintf(stderr, "SDL_CreateTexture: %s\n", SDL_GetError());
        goto cleanup;
    }

    /* --- Application state --- */
    unsigned char shadow[SCREEN_LEN] = {0};
    unsigned int  canvas_pixels[W * H];
    int           paint_color     = 1;
    int           dirty           = 1;  /* upload+refresh on first frame */
    int           need_refresh    = 1;  /* need to step+grab the Atari frame */
    int           mouse_down      = 0;
    int           running         = 1;
    Uint64        last_activity   = 0;  /* tick of last paint / key event */
    /* After the shadow was pushed, keep stepping+reading back for a
     * short grace period so the Atari frame settles visually (one
     * frame is enough for mode D, but a couple more catches any
     * OS VBI fixups). Avoids rendering a stale frame while painting. */
    const Uint64  GRACE_NS        = 200 * 1000 * 1000;   /* 200 ms */

    /* --- Main loop --- */
    while (running) {
        /* 1. Drain events */
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            switch (ev.type) {
            case SDL_EVENT_QUIT:
                running = 0;
                break;
            case SDL_EVENT_KEY_DOWN:
                switch (ev.key.key) {
                case SDLK_ESCAPE: running = 0; break;
                case SDLK_1:      paint_color = 1; break;
                case SDLK_2:      paint_color = 2; break;
                case SDLK_3:      paint_color = 3; break;
                case SDLK_C:
                    memset(shadow, 0, SCREEN_LEN);
                    dirty         = 1;
                    need_refresh  = 1;
                    last_activity = SDL_GetTicksNS();
                    break;
                }
                break;
            case SDL_EVENT_MOUSE_BUTTON_DOWN: {
                if (ev.button.button != SDL_BUTTON_LEFT) break;
                float fx = ev.button.x, fy = ev.button.y;

                /* Toolbar hit-tests first: a click on a widget
                 * selects / clears instead of starting a paint
                 * stroke. */
                int hit = 0;
                for (int i = 0; i < 3; ++i) {
                    if (in_rect(fx, fy, SWATCH_X(i), TOOLBAR_Y,
                                SWATCH_W, SWATCH_H)) {
                        paint_color  = i + 1;
                        need_refresh = 1;
                        last_activity = SDL_GetTicksNS();
                        hit = 1;
                        break;
                    }
                }
                if (!hit && in_rect(fx, fy, CLEAR_X, TOOLBAR_Y,
                                    CLEAR_W, CLEAR_H)) {
                    memset(shadow, 0, SCREEN_LEN);
                    dirty         = 1;
                    need_refresh  = 1;
                    last_activity = SDL_GetTicksNS();
                    hit = 1;
                }
                if (hit) break;

                /* Not on a widget — start painting. Paint the
                 * pixel under the cursor immediately so a single
                 * click (no drag) still leaves a mark. */
                mouse_down = 1;
                if (fx >= 0 && fx < CANVAS_W && fy >= 0 && fy < CANVAS_H) {
                    pack_pixel(shadow, (int)(fx / SCALE),
                                       (int)(fy / SCALE), paint_color);
                    dirty         = 1;
                    need_refresh  = 1;
                    last_activity = SDL_GetTicksNS();
                }
                break;
            }
            case SDL_EVENT_MOUSE_MOTION: {
                if (!mouse_down) break;
                float fx = ev.motion.x, fy = ev.motion.y;
                if (fx >= 0 && fx < CANVAS_W && fy >= 0 && fy < CANVAS_H) {
                    pack_pixel(shadow, (int)(fx / SCALE),
                                       (int)(fy / SCALE), paint_color);
                    dirty         = 1;
                    need_refresh  = 1;
                    last_activity = SDL_GetTicksNS();
                }
                break;
            }
            case SDL_EVENT_MOUSE_BUTTON_UP:
                if (ev.button.button == SDL_BUTTON_LEFT) mouse_down = 0;
                break;
            }
        }

        /* 2. Push shadow → bridge if anything changed, then advance
         *    one frame and grab the live Atari output.
         *
         *    Only hit the bridge when we actually have something
         *    new to show. Each frame(1) + rawscreen round-trip
         *    blocks the caller for ~16 ms of wall-clock emulator
         *    time plus the readback cost, so doing it every
         *    iteration regardless caps the perceived paint
         *    responsiveness at ~30 fps.  Instead we keep refreshing
         *    for a short grace window after the last paint/key
         *    event (long enough to catch any OS VBI fixups), then
         *    idle the main loop until the user does something. */
        if (need_refresh) {
            if (dirty) {
                atb_memload(c, SCREEN_ADDR, shadow, SCREEN_LEN);
                dirty = 0;
            }
            atb_frame(c, 1);
        }

        unsigned char* atari_pixels = NULL;
        size_t         atari_len    = 0;
        unsigned int   fw = 0, fh = 0;
        if (need_refresh
            && atb_rawscreen_inline(c, &atari_pixels, &atari_len, &fw, &fh) == ATB_OK
            && atari_pixels && fw > 0 && fh > 0) {
            /* (Re)create the texture when the reported frame size
             * changes — happens at least once on the first frame,
             * and again if the user changes overscan/region in the
             * server. Silently mismatching sizes is what caused
             * the "black right pane" bug in earlier versions. */
            if (atari_tex == NULL
                || (int)fw != atari_tex_w
                || (int)fh != atari_tex_h) {
                if (atari_tex) SDL_DestroyTexture(atari_tex);
                atari_tex = SDL_CreateTexture(
                    renderer, SDL_PIXELFORMAT_XRGB8888,
                    SDL_TEXTUREACCESS_STREAMING, (int)fw, (int)fh);
                if (atari_tex) {
                    SDL_SetTextureScaleMode(atari_tex, SDL_SCALEMODE_NEAREST);
                    atari_tex_w = (int)fw;
                    atari_tex_h = (int)fh;
                }
            }
            if (atari_tex) {
                SDL_UpdateTexture(atari_tex, NULL, atari_pixels,
                                  (int)(fw * 4));
            }
            free(atari_pixels);
        }

        /* 3. Render the local shadow into the canvas texture. */
        shadow_to_canvas_texture(shadow, canvas_pixels);
        SDL_UpdateTexture(canvas_tex, NULL, canvas_pixels, W * 4);

        /* 4. Present. */
        SDL_SetRenderDrawColor(renderer, 32, 32, 32, 255);
        SDL_RenderClear(renderer);

        SDL_FRect canvas_dst = { 0, 0, CANVAS_W, CANVAS_H };
        SDL_RenderTexture(renderer, canvas_tex, NULL, &canvas_dst);

        /* Letterbox the live frame into the reserved slot while
         * preserving its aspect ratio. */
        if (atari_tex && atari_tex_w > 0 && atari_tex_h > 0) {
            float sx = (float)ATARI_SLOT_W / atari_tex_w;
            float sy = (float)ATARI_SLOT_H / atari_tex_h;
            float s  = sx < sy ? sx : sy;
            float draw_w = atari_tex_w * s;
            float draw_h = atari_tex_h * s;
            SDL_FRect atari_dst = {
                CANVAS_W + GUTTER + (ATARI_SLOT_W - draw_w) * 0.5f,
                (ATARI_SLOT_H - draw_h) * 0.5f,
                draw_w, draw_h,
            };
            SDL_RenderTexture(renderer, atari_tex, NULL, &atari_dst);
        }

        /* Toolbar under the Atari frame: three clickable colour
         * swatches (mirror the 1/2/3 keyboard shortcut) and a
         * clear button (mirror the C shortcut). The currently
         * selected colour is outlined in yellow. */
        for (int i = 0; i < 3; ++i) {
            unsigned int rgb = CANVAS_RGB[i + 1];
            SDL_SetRenderDrawColor(renderer,
                (rgb >> 16) & 0xff, (rgb >> 8) & 0xff, rgb & 0xff, 255);
            SDL_FRect swatch = {
                .x = SWATCH_X(i), .y = TOOLBAR_Y,
                .w = SWATCH_W,    .h = SWATCH_H,
            };
            SDL_RenderFillRect(renderer, &swatch);
            /* Thin white border so the dark swatches are visible
             * against the dark window background. */
            SDL_SetRenderDrawColor(renderer, 200, 200, 200, 255);
            SDL_RenderRect(renderer, &swatch);
            if (i + 1 == paint_color) {
                /* Highlight the active selection with an inset
                 * yellow outline (two px thick for visibility). */
                SDL_SetRenderDrawColor(renderer, 255, 255, 0, 255);
                SDL_RenderRect(renderer, &swatch);
                SDL_FRect inner = {
                    swatch.x + 1, swatch.y + 1,
                    swatch.w - 2, swatch.h - 2,
                };
                SDL_RenderRect(renderer, &inner);
            }
        }

        /* Clear button: dark grey fill + white border + a centred
         * "X" drawn as two crossed lines (two pixels thick so it
         * reads clearly without pulling in SDL3_ttf for text). */
        SDL_FRect clear_rect = {
            .x = CLEAR_X, .y = TOOLBAR_Y,
            .w = CLEAR_W, .h = CLEAR_H,
        };
        SDL_SetRenderDrawColor(renderer, 64, 64, 64, 255);
        SDL_RenderFillRect(renderer, &clear_rect);
        SDL_SetRenderDrawColor(renderer, 220, 220, 220, 255);
        SDL_RenderRect(renderer, &clear_rect);
        {
            float pad = 8.0f;
            float x0  = clear_rect.x + pad;
            float y0  = clear_rect.y + pad;
            float x1  = clear_rect.x + clear_rect.w - pad;
            float y1  = clear_rect.y + clear_rect.h - pad;
            for (int k = 0; k < 2; ++k) {
                SDL_RenderLine(renderer, x0 + k, y0, x1 + k, y1);
                SDL_RenderLine(renderer, x1 + k, y0, x0 + k, y1);
            }
        }

        SDL_RenderPresent(renderer);

        /* 5. Decide whether the next iteration needs another bridge
         *    round-trip. We keep refreshing for GRACE_NS after the
         *    last paint/key event, then stop hitting the bridge
         *    until the user does something again. SDL_WaitEvent
         *    blocks efficiently in the idle case so the process
         *    uses ~0% CPU when nothing is happening. */
        Uint64 now = SDL_GetTicksNS();
        if (now - last_activity > GRACE_NS) {
            need_refresh = 0;
            /* Idle: block until an event arrives (or a 100 ms tick
             * so we can still notice external state changes). */
            SDL_Event peek;
            if (SDL_WaitEventTimeout(&peek, 100)) {
                SDL_PushEvent(&peek);
            }
        } else {
            need_refresh = 1;
        }
    }

cleanup:
    if (atari_tex)  SDL_DestroyTexture(atari_tex);
    if (canvas_tex) SDL_DestroyTexture(canvas_tex);
    if (renderer)   SDL_DestroyRenderer(renderer);
    if (window)     SDL_DestroyWindow(window);
    SDL_Quit();
    atb_close(c);
    return 0;
}
