{
  01_ping.pas — minimal AltirraBridge Pascal SDK example.

  Connects to a running bridge server, sends PING, runs 60 frames,
  sends PING again, and exits. The smallest possible "did the
  socket work?" smoke test. 1:1 port of sdk/c/examples/01_ping.c.

  Build (using the Pascal SDK unit in the parent directory):
    fpc -Fu.. -FU/tmp 01_ping.pas -o01_ping

  Run:
    1. Start a bridge server in one terminal. Either the lean
       headless binary shipped with this package:
           ./AltirraBridgeServer --bridge=tcp:127.0.0.1:0
       ...or, if you want a window, the GUI emulator:
           ./AltirraSDL --bridge
       Both log two lines on stderr:
           [bridge] listening on tcp:127.0.0.1:54321
           [bridge] token-file: /tmp/altirra-bridge-12345.token
    2. In a second terminal, pass the token-file path:
           ./01_ping /tmp/altirra-bridge-12345.token
}

program ping_example;
{$MODE OBJFPC}{$H+}

uses
  SysUtils, altirra_bridge;

var
  Bridge: TAltirraBridge;
begin
  if ParamCount <> 1 then
  begin
    Writeln(StdErr, 'usage: 01_ping <token-file>');
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
      Writeln('connected. server said: ', Bridge.LastResponse);

      Bridge.Ping;
      Writeln('ping ok');

      Writeln('running 60 frames...');
      Bridge.Frame(60);
      Writeln('frame returned: ', Bridge.LastResponse);

      Bridge.Ping;
      Writeln('ping ok (after frame step)');
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
