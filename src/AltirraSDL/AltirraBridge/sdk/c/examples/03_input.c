/*
 * 03_input.c — input injection example.
 *
 * Mirrors sdk/python/examples/03_input.py: connects, boots past
 * power-on, then drives joystick + console switches + keyboard,
 * verifying the input changes are visible in the PIA / GTIA / POKEY
 * state via the raw response strings.
 *
 * Build (prebuilt binary in sdk/c/examples/bin/03_input):
 *   cc -std=c99 -I.. 03_input.c ../altirra_bridge.c -o 03_input
 *   (on Windows: cl /I.. 03_input.c ..\altirra_bridge.c ws2_32.lib)
 *
 * Run:
 *   1. Start the bridge server (see 01_ping.c for details):
 *          ./AltirraBridgeServer --bridge=tcp:127.0.0.1:0
 *   2. Pass the token-file path:
 *          ./03_input /tmp/altirra-bridge-<pid>.token
 */

#include "altirra_bridge.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Print the value of a "key":"$xx" field from the most recent
 * response, with a label.  We don't want a JSON parser dependency
 * for an example, so we cheat with strstr the same way the SDK
 * itself does internally. */
static void print_field(atb_client_t* c, const char* label, const char* key) {
    const char* r = atb_last_response(c);
    char needle[64];
    snprintf(needle, sizeof needle, "\"%s\":\"", key);
    const char* p = strstr(r, needle);
    if (!p) {
        printf("  %-30s ?\n", label);
        return;
    }
    p += strlen(needle);
    char val[16] = {0};
    size_t i = 0;
    while (*p && *p != '"' && i + 1 < sizeof val) val[i++] = *p++;
    val[i] = '\0';
    printf("  %-30s %s\n", label, val);
}

int main(int argc, char** argv) {
    if (argc != 2) {
        fprintf(stderr, "usage: %s <token-file>\n", argv[0]);
        fprintf(stderr,
            "  The token-file path is printed to stderr by\n"
            "  AltirraBridgeServer (or AltirraSDL --bridge) on startup.\n");
        return 2;
    }

    atb_client_t* c = atb_create();
    if (!c) { fprintf(stderr, "out of memory\n"); return 1; }

    if (atb_connect_token_file(c, argv[1]) != ATB_OK) {
        fprintf(stderr, "connect/hello failed: %s\n", atb_last_error(c));
        atb_close(c);
        return 1;
    }

    atb_pause(c);
    atb_frame(c, 60);  /* let the OS finish booting and configure PIA */

    /* --- Joystick directions on port 0 -------------------------- */
    printf("Joystick directions on port 0 (PORTA bits 0-3):\n");
    static const char* directions[] = {
        "up", "down", "left", "right",
        "upleft", "upright", "downleft", "downright",
        "centre",
    };
    for (size_t i = 0; i < sizeof directions / sizeof directions[0]; ++i) {
        atb_joy(c, 0, directions[i], 0);
        atb_pia(c);
        char label[32];
        snprintf(label, sizeof label, "%s -> PORTA=", directions[i]);
        print_field(c, label, "PORTA");
    }

    /* --- Fire button -------------------------------------------- */
    printf("\nFire button (GTIA TRIG0 via SetControllerTrigger):\n");
    atb_joy(c, 0, "centre", 1);
    printf("  fire pressed  -> trigger now active\n");
    atb_joy(c, 0, "centre", 0);

    /* --- Console switches --------------------------------------- */
    printf("\nConsole switches:\n");
    atb_consol(c, 1, 1, 0);   /* start + select */
    atb_gtia(c);
    print_field(c, "start+select -> consol_in=", "consol_in");
    atb_consol(c, 0, 0, 0);   /* release */
    atb_gtia(c);
    print_field(c, "released     -> consol_in=", "consol_in");

    /* --- Keyboard ----------------------------------------------- */
    printf("\nKeyboard:\n");
    static const char* keys[] = { "RETURN", "SPACE", "ESC" };
    for (size_t i = 0; i < sizeof keys / sizeof keys[0]; ++i) {
        atb_key(c, keys[i], 0, 0);
        char label[32];
        snprintf(label, sizeof label, "KEY %-8s -> kbcode=", keys[i]);
        print_field(c, label, "kbcode");
    }

    /* --- Memory write round-trip -------------------------------- */
    printf("\nMemory write round-trip:\n");
    atb_poke(c, 0x600, 0x42);
    unsigned char one;
    atb_peek(c, 0x600, 1, &one);
    printf("  POKE $600 $42 -> PEEK $600 = %02x\n", one);

    atb_poke16(c, 0x602, 0x1234);
    unsigned int word;
    atb_peek16(c, 0x602, &word);
    printf("  POKE16 $602 $1234 -> PEEK16 $602 = $%04x\n", word);

    unsigned char data[64];
    for (int i = 0; i < 64; ++i) data[i] = (unsigned char)i;
    atb_memload(c, 0x700, data, 64);
    unsigned char back[64];
    atb_memdump(c, 0x700, 64, back);
    int match = memcmp(data, back, 64) == 0;
    printf("  MEMLOAD/MEMDUMP 64 bytes round-trip: %s\n", match ? "OK" : "FAIL");

    atb_close(c);
    return match ? 0 : 1;
}
