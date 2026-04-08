// AltirraBridge - Phase 5a debugger commands
//
// All commands here wrap existing public Altirra debugger / CPU /
// GTIA / POKEY / PIA APIs. Zero chip semantics are reimplemented in
// the bridge — the wire layer is strictly marshalling.
//
//   DISASM addr [count]      — disassemble N instructions starting at addr
//                              using Altirra's real ATDisassembleInsn
//                              (auto-picks 6502/65C02/65C816 from target)
//   HISTORY [count]          — last N CPU history entries (default 64,
//                              cap 4096) with PC/opcode/regs/cycle.
//                              Biggest single RE primitive; no pyA8
//                              equivalent.
//   EVAL expr                — evaluate a debugger expression string
//                              against the current CPU state. Uses
//                              IATDebugger::EvaluateThrow which handles
//                              registers, memory deref, arithmetic,
//                              and symbol lookup.
//   CALLSTACK [count]        — JSR/RTS call-stack walk via
//                              IATDebugger::GetCallStack.
//   MEMMAP                   — high-level decoded memory layout from
//                              PIA PORTB + cartridge state. Reports
//                              OS ROM, BASIC, self-test, RAM bank,
//                              cart window — no hand-rolled probing.
//   BANK_INFO                — PIA PORTB decoded as OS/BASIC/self-test
//                              enable + XL/XE CPU/ANTIC banks. Replaces
//                              pyA8 analyzer.decode_portb().
//   CART_INFO                — cartridge mapper mode + bank + size.
//   PMG                      — decoded GTIA player/missile state (HPOS,
//                              SIZE, GRAF, colors, collisions) via
//                              ATGTIAEmulator::GetRegisterState.
//   AUDIO_STATE              — decoded POKEY per-channel state
//                              (AUDF/AUDC/AUDCTL, volume, distortion,
//                              linked/fast clock) via
//                              ATPokeyEmulator::GetRegisterState.

#pragma once

#include <string>
#include <vector>

class ATSimulator;

namespace ATBridge {

std::string CmdDisasm    (ATSimulator& sim, const std::vector<std::string>& tokens);
std::string CmdHistory   (ATSimulator& sim, const std::vector<std::string>& tokens);
std::string CmdEval      (ATSimulator& sim, const std::vector<std::string>& tokens);
std::string CmdCallStack (ATSimulator& sim, const std::vector<std::string>& tokens);
std::string CmdMemMap    (ATSimulator& sim, const std::vector<std::string>& tokens);
std::string CmdBankInfo  (ATSimulator& sim, const std::vector<std::string>& tokens);
std::string CmdCartInfo  (ATSimulator& sim, const std::vector<std::string>& tokens);
std::string CmdPmg       (ATSimulator& sim, const std::vector<std::string>& tokens);
std::string CmdAudioState(ATSimulator& sim, const std::vector<std::string>& tokens);

}  // namespace ATBridge
