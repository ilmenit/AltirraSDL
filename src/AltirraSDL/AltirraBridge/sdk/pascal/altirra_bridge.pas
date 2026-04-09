{
  altirra_bridge.pas — Free Pascal client for the AltirraBridge
  scripting protocol.

  Single-file, stdlib-only. Depends on FPC's `Sockets`, `SysUtils`,
  and `Classes` units — no Lazarus, no Indy, no Synapse. Builds with
  `{$MODE OBJFPC}{$H+}` on FPC 3.2+ (the same RTL-extra Sockets unit
  is available on Linux, macOS, Windows, FreeBSD, and Android).

  This is the Pascal analogue of `sdk/c/altirra_bridge.{h,c}`. It
  covers the Phase 1-3 command set used by the example programs:
  connect, HELLO, PING, PAUSE, RESUME, FRAME, REGS, PEEK, PEEK16,
  POKE, POKE16, MEMLOAD, MEMDUMP, JOY, KEY, CONSOL, plus the raw
  state-read verbs (PIA, GTIA, ANTIC, POKEY) whose responses the
  caller parses via the LastResponse property.

  Higher-level phases (rendering, debugger, profiler) are not
  wrapped — use the `Rpc` method to send any verb and inspect
  `LastResponse`; the protocol is text-based and stable.

  See docs/PROTOCOL.md for the full wire contract, and
  docs/WRITING_A_CLIENT.md for the design rationale behind shipping
  a native Pascal client rather than an FFI wrapper over the C SDK.

  Threading: not thread-safe. Use one TAltirraBridge per thread.

  Error handling: every failing call raises EBridgeError with the
  server's error string (or the underlying socket error on
  transport failures).
}

unit altirra_bridge;

{$MODE OBJFPC}{$H+}

interface

uses
  SysUtils, Classes, Sockets;

type
  EBridgeError = class(Exception);

  { CPU register snapshot returned by TAltirraBridge.Regs. Mirrors
    the C SDK's atb_cpu_state_t: 16-bit PC (or 24-bit on 65C816),
    8-bit A/X/Y/S/P, the 8-character decoded flag string, cycle
    counter, and the CPU mode literal ("6502" / "65C02" / "65C816"). }
  TCpuState = record
    PC:     LongWord;
    A:      Byte;
    X:      Byte;
    Y:      Byte;
    S:      Byte;
    P:      Byte;
    Cycles: QWord;
    Flags:  String;   // e.g. "--1B--Z-"
    Mode:   String;   // e.g. "6502"
  end;

  { Transport-independent session token. Populated by the
    token-file reader and passed to Connect. Exposed for advanced
    callers that read the token from somewhere other than the
    on-disk file the server writes. }
  TBridgeAddress = record
    Host:  String;     // e.g. "127.0.0.1"
    Port:  Word;       // TCP port
    Token: String;     // 32-char hex
  end;

  { A raw frame captured from the running simulator via RAWSCREEN.
    The pixel format is XRGB8888 little-endian — each 32-bit word
    is $00RRGGBB, so the byte order in memory is (B, G, R, X).
    Length(Pixels) = Width * Height * 4 on success. Returned
    by TAltirraBridge.RawScreen. }
  TRawFrame = record
    Width:  Integer;
    Height: Integer;
    Pixels: TBytes;
  end;

  { The main client class. Create, call ConnectTokenFile (which
    reads the address + token from the file AltirraBridgeServer
    writes on startup and performs the HELLO handshake), then
    drive the simulator with the Phase-specific methods below. }
  TAltirraBridge = class
  private
    FSocket:       TSocket;
    FConnected:    Boolean;
    FLastResponse: String;

    // Line-oriented receive buffer. Responses can be large
    // (MEMDUMP of 64 KB is ~85 KB of base64), so a naive
    // one-byte-at-a-time reader is too slow; we read into a
    // 16 KB buffer and scan for LF.
    FRecvBuf:      array[0..16383] of Byte;
    FRecvPos:      Integer;
    FRecvLen:      Integer;

    procedure RawSend(const Data; Len: Integer);
    function  RecvByte(out B: Byte): Boolean;
    function  RecvLine: String;
    procedure SendLine(const S: String);

    function  ParseAddress(const AddrLine: String): TBridgeAddress;
    procedure CheckOk(const Resp, Context: String);

    // Tiny JSON field extractors — the same "substring scan"
    // strategy the C SDK uses. Sufficient because every field in
    // the protocol is a flat "key":value pair with no nesting in
    // the Phase 1-3 subset.
    function  FindField(const Resp, Key: String): Integer;
    function  GetStringField(const Resp, Key: String): String;
    function  GetHexField(const Resp, Key: String): QWord;
    function  GetIntField(const Resp, Key: String): Int64;
    function  GetFloatField(const Resp, Key: String): Double;
  public
    constructor Create;
    destructor  Destroy; override;

    { Read a token file (2 lines: "tcp:HOST:PORT" and the 32-hex
      session token), connect to that address, and perform the
      HELLO handshake. One-call entry point; mirrors
      atb_connect_token_file. }
    procedure ConnectTokenFile(const TokenFilePath: String);

    { Lower-level: connect to an explicit "tcp:HOST:PORT" address
      without doing HELLO. Call Hello afterwards. }
    procedure Connect(const AddrSpec: String);
    procedure Hello(const Token: String);

    { Send a raw protocol verb (no trailing newline) and read back
      one JSON line. Does NOT check "ok":true — use CheckOk on the
      return value if the caller wants exception semantics. The
      same string is also stashed in LastResponse. }
    function  Rpc(const Cmd: String): String;

    { Close the socket. Safe to call multiple times. Automatically
      called by Destroy. }
    procedure Close;

    property LastResponse: String read FLastResponse;
    property Connected:    Boolean read FConnected;

    // ---- Phase 1 — lifecycle --------------------------------------

    procedure Ping;
    procedure Pause;
    procedure ResumeEmu;   // 'Resume' is reserved by the TThread API
    procedure Frame(N: LongWord);

    // ---- Phase 2 — state read -------------------------------------

    function  Regs: TCpuState;

    { Read one byte. Wraps PEEK addr 1. }
    function  PeekByte(Addr: Word): Byte;

    { Read N bytes (1..16384) into a dynamic array. }
    function  Peek(Addr: Word; Length: LongWord): TBytes;

    { Read a little-endian 16-bit word. }
    function  Peek16(Addr: Word): Word;

    { Raw chip-state reads. The JSON response is returned verbatim
      so the caller can extract whatever fields it needs via
      LastResponse / GetStringField / GetHexField. }
    function  Antic: String;
    function  Gtia:  String;
    function  Pokey: String;
    function  Pia:   String;

    // ---- Phase 3 — state write & input ----------------------------

    procedure Poke(Addr: Word; Value: Byte);
    procedure Poke16(Addr: Word; Value: Word);

    { Hardware-register poke. Unlike Poke, which writes the
      debug-safe RAM latch and has no side effects on ANTIC /
      GTIA / POKEY / PIA registers, HwPoke routes the write
      through the real CPU bus, triggering the same chip write
      handlers a `STA $Dxxx` instruction would. Use this to drive
      ANTIC DLIST / DMACTL / NMIEN, GTIA colour registers, etc.
      from a bare-metal client that has parked the CPU via
      BootBare. Mirrors atb_hwpoke from the C SDK. }
    procedure HwPoke(Addr: Word; Value: Byte);

    { Upload Length bytes from Data into RAM at Addr. Sent inline
      as base64; works over adb-forward on Android. Max size is
      limited by the server's recv buffer cap (1 MB). }
    procedure MemLoad(Addr: Word; const Data; Length: LongWord);
    procedure MemLoadBytes(Addr: Word; const Data: TBytes);

    { Download Length bytes (1..65536) starting at Addr. }
    function  MemDump(Addr: Word; Length: LongWord): TBytes;

    { Joystick state for Port (0..3). Direction is one of
      "centre", "up", "down", "left", "right", "upleft",
      "upright", "downleft", "downright" (or compass: "n", "ne",
      "e", "se", "s", "sw", "w", "nw", "c"). Fire non-zero presses
      the trigger. }
    procedure Joy(Port: LongWord; const Direction: String; Fire: Boolean);

    { Push one keystroke through POKEY. Name is a key identifier
      ("A", "SPACE", "RETURN", "ESC", "1", ...), case-insensitive
      for letters. Named KeyPress rather than Key because FPC's
      ExtractX helpers below take a parameter called Key, and
      overload resolution would flag a shadowing conflict. }
    procedure KeyPress(const Name: String; Shift, Ctrl: Boolean);

    { Console switch state (active-low). Each True holds the
      corresponding switch down; False releases it. }
    procedure Consol(StartSw, SelectSw, OptionSw: Boolean);

    // ---- Lifecycle: bare-metal boot --------------------------------

    { Boot the server's embedded "bare-metal" stub: a ~30-byte XEX
      that disables IRQs, NMIs, BASIC, and ANTIC DMA, then parks
      the CPU in an infinite JMP * loop. After this returns, the
      client owns the machine and can drive ANTIC / GTIA / POKEY
      directly via HwPoke without any OS code modifying state
      from under it. Mirrors atb_boot_bare. The server's
      BOOT_BARE handler waits synchronously for the stub to run
      and the CPU to park, so no client-side settle is needed. }
    procedure BootBare;

    // ---- Phase 4 — rendering ---------------------------------------

    { Upload a 768-byte Adobe Color Table (256 * RGB24) and run
      the same palette-fitting solver Windows Altirra uses in its
      Color Image Reference dialog. The server updates the active
      profile's analog decoder parameters so GTIA composites
      subsequent frames through a palette that approximates the
      supplied .act. Returns the solver's final RMS error
      (same metric the Windows dialog reports). Act must be
      exactly 768 bytes. Mirrors atb_palette_load_act. }
    function  PaletteLoadAct(const Act: TBytes): Double;

    { Reset GTIA's analog decoder back to the factory NTSC / PAL
      defaults, undoing any prior PaletteLoadAct. }
    procedure PaletteReset;

    { Capture the current simulator frame as a raw XRGB8888
      little-endian pixel buffer. Frame dimensions depend on the
      server's overscan / region setting and are NOT constant —
      callers must read Result.Width / Result.Height every time
      and (re)create any downstream texture when they change.
      Mirrors atb_rawscreen_inline. }
    function  RawScreen: TRawFrame;

    { Extract a flat "key":value field from any response string.
      Exposed so callers can pull fields out of the raw chip-state
      JSON (Antic/Gtia/Pokey/Pia) without hand-rolling a JSON
      parser. All three forms return the value as string, as QWord
      parsed from "$xxxx" hex, or as signed integer, respectively. }
    function  ExtractString(const Resp, Key: String): String;
    function  ExtractHex(const Resp, Key: String): QWord;
    function  ExtractInt(const Resp, Key: String): Int64;
    function  ExtractFloat(const Resp, Key: String): Double;
  end;

implementation

const
  BASE64_CHARS: String[64] =
    'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/';

// ---------------------------------------------------------------------
//  Minimal base64 codec.  Pascal's `base64` unit lives in fcl-base,
//  which isn't always shipped alongside the compiler.  Rolling our
//  own keeps the dependency list to RTL + RTL-extra.
// ---------------------------------------------------------------------

function Base64Encode(const Data; Length: Integer): String;
var
  P: PByte;
  I, N, V: Integer;
  Bits, NumBits: Integer;
begin
  Result := '';
  if Length <= 0 then Exit;
  SetLength(Result, ((Length + 2) div 3) * 4);
  P := @Data;
  N := 0;
  Bits := 0;
  NumBits := 0;
  for I := 0 to Length - 1 do
  begin
    Bits := (Bits shl 8) or P[I];
    Inc(NumBits, 8);
    while NumBits >= 6 do
    begin
      Dec(NumBits, 6);
      V := (Bits shr NumBits) and $3F;
      Inc(N);
      Result[N] := BASE64_CHARS[V + 1];
    end;
  end;
  if NumBits > 0 then
  begin
    V := (Bits shl (6 - NumBits)) and $3F;
    Inc(N);
    Result[N] := BASE64_CHARS[V + 1];
  end;
  // Padding.
  while (N mod 4) <> 0 do
  begin
    Inc(N);
    Result[N] := '=';
  end;
  SetLength(Result, N);
end;

function Base64DecVal(C: Char): Integer;
begin
  case C of
    'A'..'Z': Result := Ord(C) - Ord('A');
    'a'..'z': Result := Ord(C) - Ord('a') + 26;
    '0'..'9': Result := Ord(C) - Ord('0') + 52;
    '+':      Result := 62;
    '/':      Result := 63;
  else
    Result := -1;
  end;
end;

function Base64Decode(const S: String): TBytes;
var
  I, V, Bits, NumBits, N: Integer;
begin
  SetLength(Result, (Length(S) * 3) div 4 + 4);
  Bits := 0;
  NumBits := 0;
  N := 0;
  for I := 1 to Length(S) do
  begin
    if (S[I] = '=') or (S[I] = #10) or (S[I] = #13) or (S[I] = ' ') then
      Continue;
    V := Base64DecVal(S[I]);
    if V < 0 then Continue;
    Bits := (Bits shl 6) or V;
    Inc(NumBits, 6);
    if NumBits >= 8 then
    begin
      Dec(NumBits, 8);
      Result[N] := (Bits shr NumBits) and $FF;
      Inc(N);
    end;
  end;
  SetLength(Result, N);
end;

// ---------------------------------------------------------------------
//  TAltirraBridge — socket I/O plumbing
// ---------------------------------------------------------------------

constructor TAltirraBridge.Create;
begin
  inherited Create;
  FSocket := -1;
  FConnected := False;
  FRecvPos := 0;
  FRecvLen := 0;
end;

destructor TAltirraBridge.Destroy;
begin
  Close;
  inherited Destroy;
end;

procedure TAltirraBridge.Close;
begin
  if FSocket <> -1 then
  begin
    CloseSocket(FSocket);
    FSocket := -1;
  end;
  FConnected := False;
end;

procedure TAltirraBridge.RawSend(const Data; Len: Integer);
var
  P: PByte;
  Rem: Integer;
  Sent: PtrInt;   // fpSend returns ssize_t = PtrInt on FPC
begin
  if not FConnected then
    raise EBridgeError.Create('send on closed bridge connection');
  P := @Data;
  Rem := Len;
  while Rem > 0 do
  begin
    Sent := fpSend(FSocket, P, Rem, 0);
    if Sent <= 0 then
      raise EBridgeError.CreateFmt(
        'send failed (errno=%d)', [SocketError]);
    Inc(P, Sent);
    Dec(Rem, Sent);
  end;
end;

function TAltirraBridge.RecvByte(out B: Byte): Boolean;
var
  N: PtrInt;  // fpRecv returns ssize_t = PtrInt on FPC
begin
  if FRecvPos >= FRecvLen then
  begin
    N := fpRecv(FSocket, @FRecvBuf[0], SizeOf(FRecvBuf), 0);
    if N <= 0 then
    begin
      Result := False;
      Exit;
    end;
    FRecvPos := 0;
    FRecvLen := N;
  end;
  B := FRecvBuf[FRecvPos];
  Inc(FRecvPos);
  Result := True;
end;

function TAltirraBridge.RecvLine: String;
var
  B: Byte;
  SB: TStringBuilder;
begin
  SB := TStringBuilder.Create(256);
  try
    while True do
    begin
      if not RecvByte(B) then
      begin
        FConnected := False;
        raise EBridgeError.Create('connection closed while reading response');
      end;
      if B = 10 then Break;     // LF terminates
      if B = 13 then Continue;  // tolerate CR
      SB.Append(Chr(B));
    end;
    Result := SB.ToString;
  finally
    SB.Free;
  end;
end;

procedure TAltirraBridge.SendLine(const S: String);
var
  Line: String;
begin
  Line := S + #10;
  if Length(Line) > 0 then
    RawSend(Line[1], Length(Line));
end;

function TAltirraBridge.Rpc(const Cmd: String): String;
begin
  SendLine(Cmd);
  Result := RecvLine;
  FLastResponse := Result;
end;

procedure TAltirraBridge.CheckOk(const Resp, Context: String);
var
  ErrPos, StartQ, EndQ: Integer;
begin
  if Pos('"ok":true', Resp) > 0 then Exit;
  // Parse out the "error" field, if present, for a nicer message.
  ErrPos := Pos('"error":"', Resp);
  if (ErrPos > 0) and (ErrPos + Length('"error":"') <= Length(Resp)) then
  begin
    StartQ := ErrPos + Length('"error":"');
    EndQ := StartQ;
    while (EndQ <= Length(Resp)) and (Resp[EndQ] <> '"') do Inc(EndQ);
    raise EBridgeError.CreateFmt('%s: %s',
      [Context, Copy(Resp, StartQ, EndQ - StartQ)]);
  end;
  raise EBridgeError.CreateFmt('%s: %s', [Context, Resp]);
end;

// ---------------------------------------------------------------------
//  Address parsing and HELLO handshake
// ---------------------------------------------------------------------

function TAltirraBridge.ParseAddress(const AddrLine: String): TBridgeAddress;
var
  S: String;
  LastColon, I: Integer;
begin
  Result.Token := '';
  S := Trim(AddrLine);
  if Copy(S, 1, 4) <> 'tcp:' then
    raise EBridgeError.CreateFmt(
      'unsupported address form (only tcp: handled): %s', [S]);
  Delete(S, 1, 4);
  LastColon := 0;
  for I := Length(S) downto 1 do
    if S[I] = ':' then
    begin
      LastColon := I;
      Break;
    end;
  if LastColon = 0 then
    raise EBridgeError.CreateFmt('bad address: %s', [S]);
  Result.Host := Copy(S, 1, LastColon - 1);
  Result.Port := StrToInt(Copy(S, LastColon + 1, MaxInt));
end;

procedure TAltirraBridge.Connect(const AddrSpec: String);
var
  Addr: TBridgeAddress;
  SockAddr: TInetSockAddr;
begin
  Close;
  Addr := ParseAddress(AddrSpec);
  FSocket := fpSocket(AF_INET, SOCK_STREAM, 0);
  if FSocket = -1 then
    raise EBridgeError.CreateFmt('socket: errno=%d', [SocketError]);
  SockAddr.sin_family := AF_INET;
  SockAddr.sin_port   := htons(Addr.Port);
  SockAddr.sin_addr   := StrToNetAddr(Addr.Host);
  if fpConnect(FSocket, @SockAddr, SizeOf(SockAddr)) <> 0 then
  begin
    CloseSocket(FSocket);
    FSocket := -1;
    raise EBridgeError.CreateFmt(
      'connect %s:%d failed (errno=%d)',
      [Addr.Host, Addr.Port, SocketError]);
  end;
  FConnected := True;
  FRecvPos := 0;
  FRecvLen := 0;
end;

procedure TAltirraBridge.Hello(const Token: String);
var
  Resp: String;
begin
  Resp := Rpc('HELLO ' + Token);
  CheckOk(Resp, 'HELLO');
end;

procedure TAltirraBridge.ConnectTokenFile(const TokenFilePath: String);
var
  Lines: TStringList;
  AddrLine, Token: String;
begin
  Lines := TStringList.Create;
  try
    Lines.LoadFromFile(TokenFilePath);
    if Lines.Count < 2 then
      raise EBridgeError.CreateFmt(
        'token file %s: expected 2 lines, got %d',
        [TokenFilePath, Lines.Count]);
    AddrLine := Trim(Lines[0]);
    Token    := Trim(Lines[1]);
  finally
    Lines.Free;
  end;
  Connect(AddrLine);
  Hello(Token);
end;

// ---------------------------------------------------------------------
//  JSON field extractors
// ---------------------------------------------------------------------

function TAltirraBridge.FindField(const Resp, Key: String): Integer;
begin
  Result := Pos('"' + Key + '":', Resp);
  if Result > 0 then
    Inc(Result, Length(Key) + 3);  // skip `"key":`
end;

function TAltirraBridge.GetStringField(const Resp, Key: String): String;
var
  P, EndQ: Integer;
begin
  P := FindField(Resp, Key);
  if P = 0 then
    raise EBridgeError.CreateFmt('field "%s" missing from response: %s',
      [Key, Resp]);
  if (P > Length(Resp)) or (Resp[P] <> '"') then
    raise EBridgeError.CreateFmt('field "%s" not a string: %s', [Key, Resp]);
  Inc(P);
  EndQ := P;
  while (EndQ <= Length(Resp)) and (Resp[EndQ] <> '"') do Inc(EndQ);
  Result := Copy(Resp, P, EndQ - P);
end;

function TAltirraBridge.GetHexField(const Resp, Key: String): QWord;
var
  P, EndP: Integer;
  S: String;
  IsHex: Boolean;
begin
  // Accept both quoted-string forms ("key":"$xxxx" / "key":"0xXX")
  // and bare numeric forms ("key":12345). This matches the C SDK's
  // atb_extract_uint() tolerance — different commands use different
  // conventions for the same kind of value.
  P := FindField(Resp, Key);
  if P = 0 then
    raise EBridgeError.CreateFmt('field "%s" missing from response: %s',
      [Key, Resp]);
  if P > Length(Resp) then
    raise EBridgeError.CreateFmt('field "%s" truncated in response', [Key]);
  IsHex := False;
  if Resp[P] = '"' then
  begin
    Inc(P);
    if (P <= Length(Resp)) and (Resp[P] = '$') then
    begin
      Inc(P);
      IsHex := True;
    end
    else if (P + 1 <= Length(Resp)) and (Resp[P] = '0')
         and ((Resp[P+1] = 'x') or (Resp[P+1] = 'X')) then
    begin
      Inc(P, 2);
      IsHex := True;
    end;
    EndP := P;
    while (EndP <= Length(Resp)) and (Resp[EndP] <> '"') do Inc(EndP);
  end
  else
  begin
    EndP := P;
    while (EndP <= Length(Resp))
          and (Resp[EndP] in ['0'..'9','a'..'f','A'..'F']) do Inc(EndP);
  end;
  S := Copy(Resp, P, EndP - P);
  if IsHex then
    Result := StrToQWord('$' + S)
  else
    Result := StrToQWord(S);
end;

function TAltirraBridge.GetIntField(const Resp, Key: String): Int64;
var
  P, EndP: Integer;
  S: String;
begin
  P := FindField(Resp, Key);
  if P = 0 then
    raise EBridgeError.CreateFmt('field "%s" missing', [Key]);
  EndP := P;
  while (EndP <= Length(Resp)) and (Resp[EndP] in ['-','0'..'9']) do
    Inc(EndP);
  S := Copy(Resp, P, EndP - P);
  Result := StrToInt64(S);
end;

function TAltirraBridge.GetFloatField(const Resp, Key: String): Double;
var
  P, EndP: Integer;
  S: String;
  FS: TFormatSettings;
begin
  // Parse a bare numeric field: integer, decimal, or scientific
  // notation. Used for PALETTE_LOAD_ACT's rms_error etc.
  P := FindField(Resp, Key);
  if P = 0 then
    raise EBridgeError.CreateFmt('field "%s" missing', [Key]);
  EndP := P;
  while (EndP <= Length(Resp))
        and (Resp[EndP] in ['-','+','0'..'9','.','e','E']) do
    Inc(EndP);
  S := Copy(Resp, P, EndP - P);
  // JSON is always '.'-separated regardless of locale; pin the
  // format settings so StrToFloat doesn't misparse on a locale
  // that uses ',' as the decimal separator.
  FS := DefaultFormatSettings;
  FS.DecimalSeparator := '.';
  FS.ThousandSeparator := #0;
  Result := StrToFloat(S, FS);
end;

function TAltirraBridge.ExtractString(const Resp, Key: String): String;
begin
  Result := GetStringField(Resp, Key);
end;

function TAltirraBridge.ExtractHex(const Resp, Key: String): QWord;
begin
  Result := GetHexField(Resp, Key);
end;

function TAltirraBridge.ExtractInt(const Resp, Key: String): Int64;
begin
  Result := GetIntField(Resp, Key);
end;

function TAltirraBridge.ExtractFloat(const Resp, Key: String): Double;
begin
  Result := GetFloatField(Resp, Key);
end;

// ---------------------------------------------------------------------
//  Phase 1 — lifecycle
// ---------------------------------------------------------------------

procedure TAltirraBridge.Ping;
begin
  CheckOk(Rpc('PING'), 'PING');
end;

procedure TAltirraBridge.Pause;
begin
  CheckOk(Rpc('PAUSE'), 'PAUSE');
end;

procedure TAltirraBridge.ResumeEmu;
begin
  CheckOk(Rpc('RESUME'), 'RESUME');
end;

procedure TAltirraBridge.Frame(N: LongWord);
begin
  CheckOk(Rpc(Format('FRAME %d', [N])), 'FRAME');
end;

// ---------------------------------------------------------------------
//  Phase 2 — state read
// ---------------------------------------------------------------------

function TAltirraBridge.Regs: TCpuState;
var
  Resp: String;
begin
  Resp := Rpc('REGS');
  CheckOk(Resp, 'REGS');
  Result.PC     := GetHexField(Resp, 'PC');
  Result.A      := GetHexField(Resp, 'A') and $FF;
  Result.X      := GetHexField(Resp, 'X') and $FF;
  Result.Y      := GetHexField(Resp, 'Y') and $FF;
  Result.S      := GetHexField(Resp, 'S') and $FF;
  Result.P      := GetHexField(Resp, 'P') and $FF;
  Result.Cycles := GetIntField(Resp, 'cycles');
  Result.Flags  := GetStringField(Resp, 'flags');
  Result.Mode   := GetStringField(Resp, 'mode');
end;

function TAltirraBridge.Peek(Addr: Word; Length: LongWord): TBytes;
var
  Resp, HexData: String;
  I, N: Integer;
  B: Byte;
begin
  if (Length = 0) or (Length > 16384) then
    raise EBridgeError.CreateFmt('Peek: bad length %d', [Length]);
  Resp := Rpc(Format('PEEK $%x %d', [Addr, Length]));
  CheckOk(Resp, 'PEEK');
  HexData := GetStringField(Resp, 'data');
  // Server emits "data" as a plain hex string, two chars per byte.
  N := System.Length(HexData) div 2;
  SetLength(Result, N);
  for I := 0 to N - 1 do
  begin
    B := StrToInt('$' + Copy(HexData, I * 2 + 1, 2));
    Result[I] := B;
  end;
end;

function TAltirraBridge.PeekByte(Addr: Word): Byte;
var
  B: TBytes;
begin
  B := Peek(Addr, 1);
  Result := B[0];
end;

function TAltirraBridge.Peek16(Addr: Word): Word;
var
  Resp: String;
begin
  Resp := Rpc(Format('PEEK16 $%x', [Addr]));
  CheckOk(Resp, 'PEEK16');
  Result := GetHexField(Resp, 'value') and $FFFF;
end;

function TAltirraBridge.Antic: String;
begin
  Result := Rpc('ANTIC');
  CheckOk(Result, 'ANTIC');
end;

function TAltirraBridge.Gtia: String;
begin
  Result := Rpc('GTIA');
  CheckOk(Result, 'GTIA');
end;

function TAltirraBridge.Pokey: String;
begin
  Result := Rpc('POKEY');
  CheckOk(Result, 'POKEY');
end;

function TAltirraBridge.Pia: String;
begin
  Result := Rpc('PIA');
  CheckOk(Result, 'PIA');
end;

// ---------------------------------------------------------------------
//  Phase 3 — state write & input
// ---------------------------------------------------------------------

procedure TAltirraBridge.Poke(Addr: Word; Value: Byte);
begin
  CheckOk(Rpc(Format('POKE $%x $%x', [Addr, Value])), 'POKE');
end;

procedure TAltirraBridge.Poke16(Addr: Word; Value: Word);
begin
  CheckOk(Rpc(Format('POKE16 $%x $%x', [Addr, Value])), 'POKE16');
end;

procedure TAltirraBridge.MemLoad(Addr: Word; const Data; Length: LongWord);
var
  Encoded: String;
begin
  if Length = 0 then Exit;
  Encoded := Base64Encode(Data, System.LongInt(Length));
  CheckOk(Rpc(Format('MEMLOAD $%x %s', [Addr, Encoded])), 'MEMLOAD');
end;

procedure TAltirraBridge.MemLoadBytes(Addr: Word; const Data: TBytes);
begin
  if System.Length(Data) = 0 then Exit;
  MemLoad(Addr, Data[0], System.Length(Data));
end;

function TAltirraBridge.MemDump(Addr: Word; Length: LongWord): TBytes;
var
  Resp, B64: String;
begin
  if (Length = 0) or (Length > 65536) then
    raise EBridgeError.CreateFmt('MemDump: bad length %d', [Length]);
  Resp := Rpc(Format('MEMDUMP $%x %d', [Addr, Length]));
  CheckOk(Resp, 'MEMDUMP');
  B64 := GetStringField(Resp, 'data');
  Result := Base64Decode(B64);
end;

procedure TAltirraBridge.Joy(Port: LongWord; const Direction: String;
  Fire: Boolean);
var
  Cmd: String;
begin
  // Wire format mirrors atb_joy: "JOY <port> <dir> fire" when the
  // trigger is held, otherwise just "JOY <port> <dir>". The literal
  // word "fire" is what the server parser looks for — a numeric
  // flag is NOT accepted.
  if Fire then
    Cmd := Format('JOY %d %s fire', [Port, Direction])
  else
    Cmd := Format('JOY %d %s', [Port, Direction]);
  CheckOk(Rpc(Cmd), 'JOY');
end;

procedure TAltirraBridge.KeyPress(const Name: String; Shift, Ctrl: Boolean);
var
  Cmd: String;
begin
  Cmd := 'KEY ' + Name;
  if Shift then Cmd := Cmd + ' shift';
  if Ctrl  then Cmd := Cmd + ' ctrl';
  CheckOk(Rpc(Cmd), 'KEY');
end;

procedure TAltirraBridge.Consol(StartSw, SelectSw, OptionSw: Boolean);
var
  Cmd: String;
begin
  // Wire format mirrors atb_consol: the verb followed by any of
  // the literal words "start", "select", "option" — each word is
  // present iff the matching switch is held down. Passing the bare
  // "CONSOL" verb releases all three.
  Cmd := 'CONSOL';
  if StartSw  then Cmd := Cmd + ' start';
  if SelectSw then Cmd := Cmd + ' select';
  if OptionSw then Cmd := Cmd + ' option';
  CheckOk(Rpc(Cmd), 'CONSOL');
end;

procedure TAltirraBridge.HwPoke(Addr: Word; Value: Byte);
begin
  CheckOk(Rpc(Format('HWPOKE $%x $%x', [Addr, Value])), 'HWPOKE');
end;

procedure TAltirraBridge.BootBare;
begin
  // The server's CmdBootBare polls for the stub's signature bytes
  // and the parked-CPU PC synchronously before acknowledging, so
  // no client-side frame settle is required.
  CheckOk(Rpc('BOOT_BARE'), 'BOOT_BARE');
end;

function TAltirraBridge.PaletteLoadAct(const Act: TBytes): Double;
var
  Encoded, Resp: String;
begin
  if System.Length(Act) <> 768 then
    raise EBridgeError.CreateFmt(
      'PaletteLoadAct: expected 768 bytes, got %d', [System.Length(Act)]);
  Encoded := Base64Encode(Act[0], 768);
  Resp := Rpc('PALETTE_LOAD_ACT ' + Encoded);
  CheckOk(Resp, 'PALETTE_LOAD_ACT');
  try
    Result := GetFloatField(Resp, 'rms_error');
  except
    // Older server builds may omit rms_error. Treat as 0.
    Result := 0.0;
  end;
end;

procedure TAltirraBridge.PaletteReset;
begin
  CheckOk(Rpc('PALETTE_RESET'), 'PALETTE_RESET');
end;

function TAltirraBridge.RawScreen: TRawFrame;
var
  Resp, B64: String;
  Expect: Integer;
begin
  Resp := Rpc('RAWSCREEN inline=true');
  CheckOk(Resp, 'RAWSCREEN');
  Result.Width  := GetIntField(Resp, 'width');
  Result.Height := GetIntField(Resp, 'height');
  if (Result.Width <= 0) or (Result.Height <= 0) then
    raise EBridgeError.CreateFmt(
      'RAWSCREEN: bad dimensions %dx%d',
      [Result.Width, Result.Height]);
  B64 := GetStringField(Resp, 'data');
  Result.Pixels := Base64Decode(B64);
  Expect := Result.Width * Result.Height * 4;
  if System.Length(Result.Pixels) <> Expect then
    raise EBridgeError.CreateFmt(
      'RAWSCREEN: expected %d bytes, got %d',
      [Expect, System.Length(Result.Pixels)]);
end;

end.
