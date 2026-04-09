{
  03_input.pas — input injection example.

  Mirrors sdk/c/examples/03_input.c: connects, boots past power-on,
  then drives joystick + console switches + keyboard, verifying the
  input changes are visible in the PIA / GTIA / POKEY state via the
  raw response strings. Finishes with a POKE / PEEK and
  MEMLOAD / MEMDUMP round-trip.

  Build:
    fpc -Fu.. -FU/tmp 03_input.pas -o03_input

  Run:
    1. Start the bridge server (see 01_ping.pas for details):
           ./AltirraBridgeServer --bridge=tcp:127.0.0.1:0
    2. Pass the token-file path:
           ./03_input /tmp/altirra-bridge-<pid>.token
}

program input_example;
{$MODE OBJFPC}{$H+}

uses
  SysUtils, altirra_bridge;

const
  DIRECTIONS: array[0..8] of String = (
    'up', 'down', 'left', 'right',
    'upleft', 'upright', 'downleft', 'downright',
    'centre');

  KEYS: array[0..2] of String = ('RETURN', 'SPACE', 'ESC');

var
  Bridge:   TAltirraBridge;
  I, J:     Integer;
  Label_:   String;
  OneByte:  TBytes;
  Word16:   Word;
  TestData, Back: TBytes;
  Match:    Boolean;

procedure PrintField(const Resp, PrintLabel, FieldKey: String);
var
  Value: String;
begin
  try
    Value := Bridge.ExtractString(Resp, FieldKey);
  except
    on EBridgeError do Value := '?';
  end;
  Writeln(Format('  %-30s %s', [PrintLabel, Value]));
end;

begin
  if ParamCount <> 1 then
  begin
    Writeln(StdErr, 'usage: 03_input <token-file>');
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

      Bridge.Pause;
      Bridge.Frame(60);   // let the OS finish booting and configure PIA

      // --- Joystick directions on port 0 ----------------------------
      Writeln('Joystick directions on port 0 (PORTA bits 0-3):');
      for I := Low(DIRECTIONS) to High(DIRECTIONS) do
      begin
        Bridge.Joy(0, DIRECTIONS[I], False);
        Bridge.Pia;
        Label_ := Format('%s -> PORTA=', [DIRECTIONS[I]]);
        PrintField(Bridge.LastResponse, Label_, 'PORTA');
      end;

      // --- Fire button -----------------------------------------------
      Writeln;
      Writeln('Fire button (GTIA TRIG0 via SetControllerTrigger):');
      Bridge.Joy(0, 'centre', True);
      Writeln('  fire pressed  -> trigger now active');
      Bridge.Joy(0, 'centre', False);

      // --- Console switches ------------------------------------------
      Writeln;
      Writeln('Console switches:');
      Bridge.Consol(True, True, False);   // start + select
      Bridge.Gtia;
      PrintField(Bridge.LastResponse,
                 'start+select -> consol_in=', 'consol_in');
      Bridge.Consol(False, False, False); // release
      Bridge.Gtia;
      PrintField(Bridge.LastResponse,
                 'released     -> consol_in=', 'consol_in');

      // --- Keyboard --------------------------------------------------
      Writeln;
      Writeln('Keyboard:');
      for I := Low(KEYS) to High(KEYS) do
      begin
        Bridge.KeyPress(KEYS[I], False, False);
        Label_ := Format('KEY %-8s -> kbcode=', [KEYS[I]]);
        // KEY's own response already carries the kbcode, so we
        // don't need to follow with POKEY.
        PrintField(Bridge.LastResponse, Label_, 'kbcode');
      end;

      // --- Memory write round-trip -----------------------------------
      Writeln;
      Writeln('Memory write round-trip:');
      Bridge.Poke($600, $42);
      OneByte := Bridge.Peek($600, 1);
      Writeln(Format('  POKE $600 $42 -> PEEK $600 = %.2x', [OneByte[0]]));

      Bridge.Poke16($602, $1234);
      Word16 := Bridge.Peek16($602);
      Writeln(Format('  POKE16 $602 $1234 -> PEEK16 $602 = $%.4x',
        [Word16]));

      SetLength(TestData, 64);
      for I := 0 to 63 do
        TestData[I] := Byte(I);
      Bridge.MemLoadBytes($700, TestData);
      Back := Bridge.MemDump($700, 64);
      Match := Length(Back) = 64;
      if Match then
        for J := 0 to 63 do
          if Back[J] <> TestData[J] then
          begin
            Match := False;
            Break;
          end;
      if Match then
        Writeln('  MEMLOAD/MEMDUMP 64 bytes round-trip: OK')
      else
        Writeln('  MEMLOAD/MEMDUMP 64 bytes round-trip: FAIL');
      if not Match then Halt(1);
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
