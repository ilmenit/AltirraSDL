{
  02_peek_regs.pas — state-read example.

  Connects, advances 60 frames so the OS has booted past the
  memo-pad screen, and prints CPU registers plus a peek of the
  RUNAD vector at $02E0 and the first 16 bytes of zero page.

  1:1 port of sdk/c/examples/02_peek_regs.c.

  Build:
    fpc -Fu.. -FU/tmp 02_peek_regs.pas -o02_peek_regs

  Run:
    1. Start the bridge server (see 01_ping.pas for the full
       rundown on --bridge and the token file):
           ./AltirraBridgeServer --bridge=tcp:127.0.0.1:0
    2. Pass the token-file path printed on stderr:
           ./02_peek_regs /tmp/altirra-bridge-<pid>.token
}

program peek_regs_example;
{$MODE OBJFPC}{$H+}

uses
  SysUtils, altirra_bridge;

var
  Bridge: TAltirraBridge;
  Cpu:    TCpuState;
  Runad:  Word;
  Zp:     TBytes;
  I:      Integer;
  Line:   String;
begin
  if ParamCount <> 1 then
  begin
    Writeln(StdErr, 'usage: 02_peek_regs <token-file>');
    Writeln(StdErr,
      '  The token-file path is printed to stderr by');
    Writeln(StdErr,
      '  AltirraBridgeServer (or AltirraSDL --bridge) on startup.');
    Halt(2);
  end;

  Bridge := TAltirraBridge.Create;
  try
    try
      Bridge.ConnectTokenFile(ParamStr(1));

      // Pause and step 60 frames so the OS is past power-on.
      Bridge.Pause;
      Bridge.Frame(60);

      Cpu := Bridge.Regs;
      Writeln(Format(
        'CPU: PC=$%.4x A=$%.2x X=$%.2x Y=$%.2x S=$%.2x P=$%.2x',
        [Cpu.PC, Cpu.A, Cpu.X, Cpu.Y, Cpu.S, Cpu.P]));
      Writeln(Format(
        '     flags=%s mode=%s cycles=%u',
        [Cpu.Flags, Cpu.Mode, Cpu.Cycles]));

      // Peek the RUNAD vector at $02E0 — XEX run address
      // (zero if no program loaded).
      Runad := Bridge.Peek16($02E0);
      Writeln(Format('RUNAD ($02E0): $%.4x', [Runad]));

      // Peek 16 bytes of zero page (Atari OS variables live here).
      Zp := Bridge.Peek($0080, 16);
      Line := 'zero page $80..$8F: ';
      for I := 0 to Length(Zp) - 1 do
        Line := Line + Format('%.2x ', [Zp[I]]);
      Writeln(Line);
    except
      on E: EBridgeError do
      begin
        Writeln(StdErr, 'bridge error: ', E.Message);
        Halt(1);
      end;
    end;
  finally
    Bridge.Free;
  end;
end.
