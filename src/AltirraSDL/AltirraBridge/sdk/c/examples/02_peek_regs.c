/*
 * 02_peek_regs.c — state-read example.
 *
 * Connects, advances 60 frames so the OS has booted past the
 * memo-pad screen, and prints CPU registers plus a peek of the
 * RUNAD vector at $02E0 and the first 16 bytes of zero page.
 *
 * Build (prebuilt binary in sdk/c/examples/bin/02_peek_regs):
 *   cc -std=c99 -I.. 02_peek_regs.c ../altirra_bridge.c -o 02_peek_regs
 *
 * Run:
 *   1. Start the bridge server (see 01_ping.c for the full
 *      rundown on --bridge and the token file):
 *          ./AltirraBridgeServer --bridge=tcp:127.0.0.1:0
 *   2. Pass the token-file path printed on stderr:
 *          ./02_peek_regs /tmp/altirra-bridge-<pid>.token
 */

#include "altirra_bridge.h"

#include <stdio.h>
#include <stdlib.h>

int main(int argc, char** argv) {
	if (argc != 2) {
		fprintf(stderr, "usage: %s <token-file>\n", argv[0]);
		fprintf(stderr,
			"  The token-file path is printed to stderr by\n"
			"  AltirraBridgeServer (or AltirraSDL --bridge) on startup.\n");
		return 2;
	}

	atb_client_t* c = atb_create();
	if (atb_connect_token_file(c, argv[1]) != ATB_OK) {
		fprintf(stderr, "connect: %s\n", atb_last_error(c));
		atb_close(c);
		return 1;
	}

	/* Pause and step 60 frames so the OS is past power-on. */
	atb_pause(c);
	if (atb_frame(c, 60) != ATB_OK) {
		fprintf(stderr, "frame: %s\n", atb_last_error(c));
		atb_close(c);
		return 1;
	}

	/* Read CPU registers. */
	atb_cpu_state_t cpu;
	if (atb_regs(c, &cpu) != ATB_OK) {
		fprintf(stderr, "regs: %s\n", atb_last_error(c));
		atb_close(c);
		return 1;
	}
	printf("CPU: PC=$%04x A=$%02x X=$%02x Y=$%02x S=$%02x P=$%02x\n",
		cpu.pc, cpu.a, cpu.x, cpu.y, cpu.s, cpu.p);
	printf("     flags=%s mode=%s cycles=%lu\n",
		cpu.flags, cpu.mode, cpu.cycles);

	/* Peek the RUNAD vector at $02E0 — XEX run address (zero if no
	 * program loaded). */
	unsigned int runad = 0;
	if (atb_peek16(c, 0x02E0, &runad) == ATB_OK) {
		printf("RUNAD ($02E0): $%04x\n", runad);
	}

	/* Peek 16 bytes of zero page (where Atari OS variables live). */
	unsigned char zp[16];
	if (atb_peek(c, 0x0080, sizeof zp, zp) == ATB_OK) {
		printf("zero page $80..$8F: ");
		for (int i = 0; i < 16; ++i) printf("%02x ", zp[i]);
		printf("\n");
	}

	atb_close(c);
	return 0;
}
