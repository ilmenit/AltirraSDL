// AltirraBridge — bare-metal boot stub
//
// Tiny 6502 program that disables every piece of the OS that would
// fight a client trying to use the Atari as a raw display device:
//
//   - SEI         disable IRQs
//   - CLD         clear decimal mode (6502 hygiene)
//   - STA $D301   disable BASIC cart (PORTB = $FF)
//   - STA $D40E   NMIEN = 0, no VBI / DLI / reset NMI
//   - STA $D400   DMACTL = 0, ANTIC idle (client re-enables when it
//                              has written a DL and wants to draw)
//   - JMP *       park the CPU forever
//
// After the stub runs, the client owns the machine: it can POKE
// ANTIC's DLISTL/DLISTH directly, install a display list, set
// DMACTL to wake ANTIC up, and blit pixels into screen RAM without
// any OS VBI ever changing the state from under it.
//
// Rationale for this design:
//
// - 04_paint (and any similar "emulator-as-display" client) used to
//   cooperate with the running OS by poking SDLSTL/SDLSTH shadow
//   registers and trusting the OS VBI to propagate them to ANTIC.
//   That works against the real Atari OS but NOT against AltirraOS
//   (the GPL-clean stub kernel the bridge server boots by default),
//   which does not perform that VBI copy in every path. The result
//   is a screen that reverts to the OS text mode.
//
// - Rather than tailor the client to each kernel variant, we park
//   the CPU and take over completely. This also eliminates every
//   other "the OS is doing something you didn't ask for" class of
//   bug: no screen re-init on cart change, no CIO vector surprises,
//   no VBI DLI installation, no cursor blinking on top of our DL.
//
// The stub is shipped as an embedded 6502 byte array inside the
// server binary. At BOOT_BARE time the server writes the bytes to
// a cross-platform temp file and routes through the existing
// ATBridgeDispatchBoot path (the same code that handles BOOT <path>).
// The temp file is created on demand and reused across calls.
//
// Reference assembly source lives in
//   sdk/assets/bare_metal.s
// for documentation purposes. The canonical copy is the byte array
// defined in bridge_bare_stub.cpp; keep the two in sync.

#ifndef f_AT_BRIDGE_BARE_STUB_H
#define f_AT_BRIDGE_BARE_STUB_H

#include <string>

namespace ATBridge {

// Write the embedded bare-metal XEX to a cross-platform temp file
// (idempotent — uses a fixed per-process name so the file is
// overwritten rather than leaking new files) and return its UTF-8
// path. Returns an empty string on failure. The caller should pass
// the result to ATBridgeDispatchBoot() to actually load it.
std::string EnsureBareStubXexPath();

}  // namespace ATBridge

#endif
