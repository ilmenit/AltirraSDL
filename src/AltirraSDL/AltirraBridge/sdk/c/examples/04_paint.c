/*
 * 04_paint.c — Phase 4 rendering example: drive the Atari as a
 * custom display device, interactive SDL3 paint version.
 *
 * Mirrors sdk/python/examples/04_paint.py: a paint window with
 * mouse drawing on the left and a live preview of the actual
 * Atari frame on the right. Click+drag to paint, press 1/2/3 to
 * switch active color, C to clear, ESC or close window to quit.
 *
 * The C SDK itself is libc-only by design. THIS EXAMPLE adds an
 * SDL3 dependency for the window and event loop — drop in any
 * other UI library (raylib, SDL2, X11, Win32) by replacing the
 * "Render / Event loop" section. The bridge calls are
 * framework-agnostic and identical to 04_paint.py.
 *
 * What it demonstrates (identical to the Python version):
 *
 *   1. Building a custom ANTIC mode D display list and pushing it
 *      to RAM via atb_memload.
 *   2. Pointing the OS display-list shadow registers ($0230/$0231)
 *      at our DL so the OS VBI re-asserts it every frame.
 *   3. Setting color registers via the OS shadows ($02C4-$02C8).
 *   4. Mode D pixel packing — 4 pixels per byte at 2 bpp, MSB-first.
 *   5. Bulk shadow → server upload via memload (one trip per
 *      refresh instead of per-pixel POKEs).
 *   6. Reading the live frame back via atb_rawscreen_inline and
 *      blitting it to an SDL3 texture.
 *
 * Build (with SDL3 installed system-wide):
 *   cd sdk/c/examples
 *   mkdir build && cd build
 *   cmake .. -DCMAKE_BUILD_TYPE=Release
 *   cmake --build .
 *
 * Run:
 *   ./04_paint /tmp/altirra-bridge-<pid>.token
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

#define SDLSTL  0x0230
#define SDLSTH  0x0231
#define COLOR0  0x02C4    /* mode D pixel value 1 -> COLPF0 */
#define COLOR1  0x02C5    /* mode D pixel value 2 -> COLPF1 */
#define COLOR2  0x02C6    /* mode D pixel value 3 -> COLPF2 */
#define COLOR4  0x02C8    /* mode D pixel value 0 -> COLBK  */

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

static int setup_screen(atb_client_t* c) {
    unsigned char dl[128];
    size_t dl_len = build_display_list(dl);
    unsigned char clear[SCREEN_LEN] = {0};
    if (atb_pause(c) != ATB_OK) return -1;
    if (atb_memload(c, DL_ADDR,     dl,    (unsigned int)dl_len) != ATB_OK) return -1;
    if (atb_memload(c, SCREEN_ADDR, clear, SCREEN_LEN)           != ATB_OK) return -1;
    if (atb_poke(c, SDLSTL, DL_ADDR & 0xFF) != ATB_OK) return -1;
    if (atb_poke(c, SDLSTH, DL_ADDR >> 8)   != ATB_OK) return -1;
    if (atb_poke(c, COLOR4, 0x00)             != ATB_OK) return -1;
    if (atb_poke(c, COLOR0, ATARI_PALETTE[0]) != ATB_OK) return -1;
    if (atb_poke(c, COLOR1, ATARI_PALETTE[1]) != ATB_OK) return -1;
    if (atb_poke(c, COLOR2, ATARI_PALETTE[2]) != ATB_OK) return -1;
    if (atb_resume(c) != ATB_OK) return -1;
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
#define ATARI_W       336               /* Altirra NTSC frame size */
#define ATARI_H       240
#define GUTTER        8
#define WINDOW_W      (CANVAS_W + GUTTER + ATARI_W)
#define WINDOW_H      ((CANVAS_H > ATARI_H) ? CANVAS_H : ATARI_H)

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

    if (setup_screen(c) != 0) {
        fprintf(stderr, "setup failed: %s\n", atb_last_error(c));
        atb_close(c);
        return 1;
    }

    /* --- SDL3 --- */
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        atb_close(c);
        return 1;
    }

    SDL_Window*   window  = SDL_CreateWindow(
        "AltirraBridge paint demo  [1/2/3 = color   C = clear   ESC = quit]",
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

    /* Drawing canvas: 160 x 96 logical, scaled by renderer */
    SDL_Texture* canvas_tex = SDL_CreateTexture(
        renderer, SDL_PIXELFORMAT_XRGB8888,
        SDL_TEXTUREACCESS_STREAMING, W, H);
    SDL_SetTextureScaleMode(canvas_tex, SDL_SCALEMODE_NEAREST);

    /* Live Atari frame: 336 x 240, format matches the bridge's
     * RAWSCREEN payload (XRGB8888 little-endian, byte order BGRX) */
    SDL_Texture* atari_tex = SDL_CreateTexture(
        renderer, SDL_PIXELFORMAT_XRGB8888,
        SDL_TEXTUREACCESS_STREAMING, ATARI_W, ATARI_H);
    SDL_SetTextureScaleMode(atari_tex, SDL_SCALEMODE_NEAREST);

    if (!canvas_tex || !atari_tex) {
        fprintf(stderr, "SDL_CreateTexture: %s\n", SDL_GetError());
        goto cleanup;
    }

    /* --- Application state --- */
    unsigned char shadow[SCREEN_LEN] = {0};
    unsigned int  canvas_pixels[W * H];
    int           paint_color = 1;
    int           dirty       = 1;     /* upload+refresh on first frame */
    int           mouse_down  = 0;
    int           running     = 1;

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
                    dirty = 1;
                    break;
                }
                break;
            case SDL_EVENT_MOUSE_BUTTON_DOWN:
                if (ev.button.button == SDL_BUTTON_LEFT) mouse_down = 1;
                /* fallthrough */
            case SDL_EVENT_MOUSE_MOTION: {
                if (ev.type == SDL_EVENT_MOUSE_MOTION && !mouse_down) break;
                /* The renderer has logical coords; ev.motion.x/y are
                 * already in logical space because we set logical
                 * presentation. Translate logical → mode D px. */
                float fx = (ev.type == SDL_EVENT_MOUSE_MOTION)
                            ? ev.motion.x : ev.button.x;
                float fy = (ev.type == SDL_EVENT_MOUSE_MOTION)
                            ? ev.motion.y : ev.button.y;
                if (fx >= 0 && fx < CANVAS_W && fy >= 0 && fy < CANVAS_H) {
                    int x = (int)(fx / SCALE);
                    int y = (int)(fy / SCALE);
                    pack_pixel(shadow, x, y, paint_color);
                    dirty = 1;
                }
                break;
            }
            case SDL_EVENT_MOUSE_BUTTON_UP:
                if (ev.button.button == SDL_BUTTON_LEFT) mouse_down = 0;
                break;
            }
        }

        /* 2. Push shadow → bridge if anything changed, then advance
         *    one frame and grab the live Atari output. */
        if (dirty) {
            atb_memload(c, SCREEN_ADDR, shadow, SCREEN_LEN);
            dirty = 0;
        }
        atb_frame(c, 1);

        unsigned char* atari_pixels = NULL;
        size_t         atari_len    = 0;
        unsigned int   fw = 0, fh = 0;
        if (atb_rawscreen_inline(c, &atari_pixels, &atari_len, &fw, &fh) == ATB_OK
            && atari_pixels && fw == ATARI_W && fh == ATARI_H) {
            SDL_UpdateTexture(atari_tex, NULL, atari_pixels,
                              (int)(ATARI_W * 4));
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

        SDL_FRect atari_dst  = { CANVAS_W + GUTTER, 0, ATARI_W, ATARI_H };
        SDL_RenderTexture(renderer, atari_tex, NULL, &atari_dst);

        /* Tiny color swatches above the Atari frame so the user
         * can see which colour they have selected. */
        for (int i = 1; i <= 3; ++i) {
            unsigned int rgb = CANVAS_RGB[i];
            SDL_SetRenderDrawColor(renderer,
                (rgb >> 16) & 0xff, (rgb >> 8) & 0xff, rgb & 0xff, 255);
            SDL_FRect swatch = {
                .x = CANVAS_W + GUTTER + (i - 1) * 28.0f,
                .y = ATARI_H + 8,
                .w = 24, .h = 24,
            };
            SDL_RenderFillRect(renderer, &swatch);
            if (i == paint_color) {
                SDL_SetRenderDrawColor(renderer, 255, 255, 0, 255);
                SDL_RenderRect(renderer, &swatch);
            }
        }

        SDL_RenderPresent(renderer);
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
