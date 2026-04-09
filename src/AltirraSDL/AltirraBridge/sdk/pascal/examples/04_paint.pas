{
  04_paint.pas — "emulator as raw display device" example.

  Mouse-driven paint program that drives Altirra directly as a
  160x96 4-colour ANTIC mode D framebuffer. The left half of the
  window is a local paint canvas; the right half shows the
  corresponding live Atari frame, rendered by the real Altirra
  emulator through the bridge.

  1:1 port of sdk/c/examples/04_paint.c, using the same display
  list / HWPOKE / BOOT_BARE / PALETTE_LOAD_ACT sequence.

  What this example shows:

    1. Bridge.BootBare — boots the server's embedded 30-byte stub
       that disables every piece of the OS that would fight a
       client trying to draw (IRQs, NMIs / VBI / DLI, BASIC cart,
       ANTIC DMA) and then parks the CPU in an infinite JMP *
       loop. After this returns the client owns the machine
       entirely.

    2. Bridge.PaletteLoadAct — loads an Adobe Color Table (.act)
       file and runs Altirra's palette-fitting solver so GTIA
       composites subsequent frames through the supplied palette.
       The .act ships next to the example binary as g2f.act (the
       palette Graph2Font uses by default).

    3. Direct ANTIC / GTIA register writes — with the CPU parked
       and no OS VBI running, the client pokes $D402/$D403 to
       install a display list, $D400 to enable ANTIC DMA, and
       $D016-$D01A for the playfield colour registers. None of
       these writes are ever fought with by the running kernel.

    4. Mode D pixel packing — 4 pixels per byte at 2 bpp MSB-first.

    5. Bulk shadow -> server upload via Bridge.MemLoadBytes (one
       round trip per paint batch, not per pixel).

    6. Reading the live frame back via Bridge.RawScreen and
       blitting it to an SDL3 texture.

  The Pascal SDK unit itself is stdlib-only by design. THIS
  EXAMPLE adds an SDL3 dependency via the community
  SDL3-for-Pascal binding (zlib licensed, same as SDL itself) for
  the window and event loop. See sdk/pascal/README.md for the
  one-liner that fetches it.

  Controls:
    click swatch / 1,2,3   select paint colour
    click X      / C       clear canvas
    ESC or close window    quit

  Build (assuming SDL3-for-Pascal checked out at
  ./sdl3-for-pascal and libSDL3.so.0 available system-wide):

    fpc -Fu.. -Fu./sdl3-for-pascal/units 04_paint.pas -o04_paint

  Run:
    ./04_paint /tmp/altirra-bridge-<pid>.token

  The example looks for g2f.act in the following locations (in
  order), skipping the .act load if none is found:
    1. $ALTIRRA_BRIDGE_ACT environment variable (absolute path)
    2. g2f.act next to the executable
    3. ../assets/g2f.act (when run from sdk/pascal/examples/bin/)
    4. ../../assets/g2f.act (when run from a build tree)
    5. g2f.act in the current working directory
}

program paint_example;

{$MODE OBJFPC}{$H+}

uses
  SysUtils, Classes, ctypes, altirra_bridge, SDL3;

// =====================================================================
// Atari layout
// =====================================================================

const
  // Display list lives in low free RAM, screen memory just above it.
  // Both addresses chosen so the screen (3840 bytes for mode D) does
  // not cross a 4 KB boundary, which ANTIC playfield DMA cannot do.
  DL_ADDR     = $1000;
  SCREEN_ADDR = $2000;

  // ANTIC mode D = "Graphics 7": 160 x 96 pixels, 4 colors, 2 bpp,
  // 40 bytes/row, each row is 2 scanlines tall.
  W           = 160;
  H           = 96;
  ROW_BYTES   = 40;
  SCREEN_LEN  = ROW_BYTES * H;    // 3840

  // GTIA / ANTIC hardware registers. Because we boot the bare-metal
  // stub before touching any of these, no VBI / DLI / OS code ever
  // runs after the stub parks the CPU, so we can write the hardware
  // registers directly and they stay written until we overwrite
  // them ourselves.
  ANTIC_DMACTL = $D400;
  ANTIC_DLISTL = $D402;
  ANTIC_DLISTH = $D403;
  GTIA_COLPF0  = $D016;
  GTIA_COLPF1  = $D017;
  GTIA_COLPF2  = $D018;
  GTIA_COLBK   = $D01A;

  // Atari color values (hue << 4 | luma) — same palette as the
  // Python and C versions: white, red, green, on black.
  ATARI_PALETTE: array[0..2] of Byte = ($0F, $46, $C8);

// Crude RGB approximations for the on-screen drawing canvas.
// The right-side preview uses the real Altirra palette.
// Index 0 is background (black); 1..3 are the three paint colours.
const
  CANVAS_RGB: array[0..3] of cuint32 = (
    $000000,  // background
    $FFFFFF,  // "white"
    $CC3030,  // "red"
    $30B030   // "green"
  );

// =====================================================================
// Window layout
// =====================================================================

const
  SCALE         = 4;                           // drawing canvas scale
  CANVAS_W      = W * SCALE;                   // 640
  CANVAS_H      = H * SCALE;                   // 384
  // Reserve a generous slot on the right for the live Atari frame.
  // The actual frame size reported by RAWSCREEN depends on the
  // server's overscan / region settings, so we adapt at runtime.
  ATARI_SLOT_W  = 456;
  ATARI_SLOT_H  = 312;
  GUTTER        = 8;
  WINDOW_W      = CANVAS_W + GUTTER + ATARI_SLOT_W;
  TOOLBAR_H     = 48;
  WINDOW_H      = CANVAS_H + TOOLBAR_H;        // CANVAS_H > slot+toolbar

  // Toolbar widget rects. Shared between the hit-test in the event
  // loop and the draw code in the render pass.
  TOOLBAR_Y     = ATARI_SLOT_H + 8;
  SWATCH_W      = 28;
  SWATCH_H      = 28;
  SWATCH_STRIDE = 34;
  CLEAR_W       = 72;
  CLEAR_H       = 28;

function SwatchX(I: Integer): Single;
begin
  Result := CANVAS_W + GUTTER + I * SWATCH_STRIDE;
end;

function ClearX: Single;
begin
  Result := CANVAS_W + GUTTER + 3 * SWATCH_STRIDE + 16;
end;

// =====================================================================
// Mode D pixel pack / unpack
//
//     byte = | px0 (b7-6) | px1 (b5-4) | px2 (b3-2) | px3 (b1-0) |
// =====================================================================

procedure PackPixel(var Buf: array of Byte; X, Y, Color: Integer);
var
  Offset, Shift, Mask: Integer;
begin
  if (X < 0) or (X >= W) or (Y < 0) or (Y >= H) then Exit;
  if (Color < 0) or (Color > 3) then Exit;
  Offset := Y * ROW_BYTES + (X shr 2);
  Shift  := (3 - (X and 3)) * 2;
  Mask   := $03 shl Shift;
  Buf[Offset] := (Buf[Offset] and not Mask) or (Color shl Shift);
end;

function ReadPixel(const Buf: array of Byte; X, Y: Integer): Integer;
var
  Offset, Shift: Integer;
begin
  Offset := Y * ROW_BYTES + (X shr 2);
  Shift  := (3 - (X and 3)) * 2;
  Result := (Buf[Offset] shr Shift) and $03;
end;

// =====================================================================
// Display list builder
// =====================================================================

function BuildDisplayList(var Dl: array of Byte): Integer;
var
  N, I: Integer;
begin
  N := 0;
  // 3 x 8 = 24 blank scanlines: required at the top of every DL.
  Dl[N] := $70; Inc(N);
  Dl[N] := $70; Inc(N);
  Dl[N] := $70; Inc(N);
  // Mode D ($0D) with LMS bit ($40) set: $4D, then screen lo/hi.
  Dl[N] := $4D; Inc(N);
  Dl[N] := SCREEN_ADDR and $FF; Inc(N);
  Dl[N] := SCREEN_ADDR shr 8;   Inc(N);
  // 95 more mode D rows.
  for I := 1 to H - 1 do
  begin
    Dl[N] := $0D;
    Inc(N);
  end;
  // JVB ($41): jump and wait for next vertical blank.
  Dl[N] := $41; Inc(N);
  Dl[N] := DL_ADDR and $FF; Inc(N);
  Dl[N] := DL_ADDR shr 8;   Inc(N);
  Result := N;
end;

// =====================================================================
// Hit-test helper
// =====================================================================

function InRect(Px, Py, Rx, Ry, Rw, Rh: Single): Boolean;
begin
  Result := (Px >= Rx) and (Px < Rx + Rw)
        and (Py >= Ry) and (Py < Ry + Rh);
end;

// =====================================================================
// Shadow -> XRGB8888 canvas texture
// =====================================================================

procedure ShadowToCanvasTexture(const Shadow: array of Byte;
                                var Pixels: array of cuint32);
var
  Y, X, C: Integer;
begin
  for Y := 0 to H - 1 do
    for X := 0 to W - 1 do
    begin
      C := ReadPixel(Shadow, X, Y);
      Pixels[Y * W + X] := CANVAS_RGB[C];
    end;
end;

// =====================================================================
// Set up the machine: bare-metal boot, display list, colours
// =====================================================================

procedure SetupMachine(Bridge: TAltirraBridge);
var
  Dl: array[0..127] of Byte;
  DlLen: Integer;
  Clear: TBytes;
begin
  // The server's BOOT_BARE handler waits for the stub to finish
  // loading and the CPU to park synchronously, so we don't need
  // a client-side frame-settle handshake.
  Bridge.BootBare;

  DlLen := BuildDisplayList(Dl);
  SetLength(Clear, SCREEN_LEN);
  FillChar(Clear[0], SCREEN_LEN, 0);

  Bridge.MemLoad(DL_ADDR, Dl[0], DlLen);
  Bridge.MemLoadBytes(SCREEN_ADDR, Clear);

  // Point ANTIC at the custom DL. Uses HwPoke (not Poke) because
  // the debug-safe Poke path bypasses I/O register write handlers —
  // writing to the RAM latch at $D402 has no effect on ANTIC's
  // real DLISTL/DLISTH. HwPoke goes through the same CPU bus path
  // as a STA $D402 instruction would.
  Bridge.HwPoke(ANTIC_DLISTL, DL_ADDR and $FF);
  Bridge.HwPoke(ANTIC_DLISTH, DL_ADDR shr 8);

  // Playfield colour registers.
  Bridge.HwPoke(GTIA_COLBK,  $00);
  Bridge.HwPoke(GTIA_COLPF0, ATARI_PALETTE[0]);
  Bridge.HwPoke(GTIA_COLPF1, ATARI_PALETTE[1]);
  Bridge.HwPoke(GTIA_COLPF2, ATARI_PALETTE[2]);

  // Finally enable ANTIC DMA: $22 = playfield on, normal width,
  // single-line DMA. Written last so ANTIC starts fetching from
  // the right DL address on the very next scan.
  Bridge.HwPoke(ANTIC_DMACTL, $22);
end;

// =====================================================================
// .act palette loader
// =====================================================================

function TryLoadAct(Bridge: TAltirraBridge; const Argv0: String): Boolean;
var
  ExeDir, EnvPath, Chosen: String;
  Candidates: array[0..4] of String;
  N, I: Integer;
  F: TFileStream;
  Act: TBytes;
  RmsError: Double;
begin
  Result := False;
  ExeDir := ExtractFilePath(Argv0);

  N := 0;
  EnvPath := GetEnvironmentVariable('ALTIRRA_BRIDGE_ACT');
  if EnvPath <> '' then begin Candidates[N] := EnvPath; Inc(N); end;
  if ExeDir <> '' then
  begin
    Candidates[N] := IncludeTrailingPathDelimiter(ExeDir) + 'g2f.act';
    Inc(N);
    Candidates[N] := IncludeTrailingPathDelimiter(ExeDir) + '..' +
                     PathDelim + 'assets' + PathDelim + 'g2f.act';
    Inc(N);
    Candidates[N] := IncludeTrailingPathDelimiter(ExeDir) + '..' +
                     PathDelim + '..' + PathDelim + 'assets' +
                     PathDelim + 'g2f.act';
    Inc(N);
  end;
  Candidates[N] := 'g2f.act';
  Inc(N);

  Chosen := '';
  for I := 0 to N - 1 do
    if FileExists(Candidates[I]) then
    begin
      Chosen := Candidates[I];
      Break;
    end;

  if Chosen = '' then
  begin
    Writeln(StdErr,
      'note: no g2f.act palette file found — continuing with the');
    Writeln(StdErr,
      '      server''s default NTSC palette. Set ALTIRRA_BRIDGE_ACT');
    Writeln(StdErr,
      '      or place g2f.act next to the binary to enable it.');
    Flush(StdErr);
    Exit;
  end;

  SetLength(Act, 768);
  F := TFileStream.Create(Chosen, fmOpenRead or fmShareDenyNone);
  try
    if F.Size < 768 then
    begin
      Writeln(StdErr, Format(
        'error: %s is %d bytes, expected 768', [Chosen, F.Size]));
      Exit;
    end;
    F.ReadBuffer(Act[0], 768);
  finally
    F.Free;
  end;

  try
    RmsError := Bridge.PaletteLoadAct(Act);
    Writeln(StdErr, Format(
      'loaded .act palette from %s (solver RMS error %.2f)',
      [Chosen, RmsError]));
    Flush(StdErr);
    Result := True;
  except
    on E: EBridgeError do
    begin
      Writeln(StdErr, 'PALETTE_LOAD_ACT failed: ', E.Message);
      Flush(StdErr);
    end;
  end;
end;

// =====================================================================
// Main loop
// =====================================================================

var
  Bridge:      TAltirraBridge;
  Window:      PSDL_Window;
  Renderer:    PSDL_Renderer;
  CanvasTex:   PSDL_Texture;
  AtariTex:    PSDL_Texture;
  AtariTexW:   Integer;
  AtariTexH:   Integer;

  Shadow:        array[0..SCREEN_LEN - 1] of Byte;
  CanvasPixels:  array[0..W * H - 1] of cuint32;
  ShadowBytes:   TBytes;

  PaintColor:   Integer;
  Dirty:        Boolean;
  NeedRefresh:  Boolean;
  MouseDown:    Boolean;
  Running:      Boolean;
  LastActivity: cuint64;
  Now:          cuint64;

  Ev:           TSDL_Event;
  Peek:         TSDL_Event;
  Frame:        TRawFrame;
  CanvasDst:    TSDL_FRect;
  AtariDst:     TSDL_FRect;
  Swatch:       TSDL_FRect;
  Inner:        TSDL_FRect;
  ClearRect:    TSDL_FRect;
  Hit:          Boolean;
  I, K:         Integer;
  Rgb:          cuint32;
  Sx, Sy, S:    Single;
  DrawW, DrawH: Single;
  Pad, X0, Y0, X1, Y1: Single;
  Fx, Fy:       Single;

const
  // Grace window after last user input during which we keep
  // stepping the simulator and re-reading the rawscreen so the
  // Atari frame settles visually. 200 ms is plenty at 60 Hz.
  GRACE_NS: cuint64 = 200 * 1000 * 1000;

begin
  if ParamCount <> 1 then
  begin
    Writeln(StdErr, 'usage: 04_paint <token-file>');
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
      SetupMachine(Bridge);
      TryLoadAct(Bridge, ParamStr(0));
    except
      on E: EBridgeError do
      begin
        Writeln(StdErr, 'bridge setup failed: ', E.Message);
        Bridge.Free;
        Halt(1);
      end;
    end;

    // --- SDL3 init -------------------------------------------------
    if not SDL_Init(SDL_INIT_VIDEO) then
    begin
      Writeln(StdErr, 'SDL_Init: ', SDL_GetError);
      Bridge.Free;
      Halt(1);
    end;

    Window := SDL_CreateWindow(
      'AltirraBridge paint demo  '
      + '[click swatch or 1/2/3 = colour   '
      + 'click X or C = clear   ESC = quit]',
      WINDOW_W, WINDOW_H, 0);
    if Window = nil then
    begin
      Writeln(StdErr, 'SDL_CreateWindow: ', SDL_GetError);
      SDL_Quit;
      Bridge.Free;
      Halt(1);
    end;

    Renderer := SDL_CreateRenderer(Window, nil);
    if Renderer = nil then
    begin
      Writeln(StdErr, 'SDL_CreateRenderer: ', SDL_GetError);
      SDL_DestroyWindow(Window);
      SDL_Quit;
      Bridge.Free;
      Halt(1);
    end;

    SDL_SetRenderLogicalPresentation(Renderer, WINDOW_W, WINDOW_H,
      SDL_LOGICAL_PRESENTATION_LETTERBOX);
    // Not a game — don't wait for vblank between presents.
    SDL_SetRenderVSync(Renderer, 0);

    // Drawing canvas: 160 x 96 logical, scaled by renderer.
    CanvasTex := SDL_CreateTexture(
      Renderer, SDL_PIXELFORMAT_XRGB8888,
      SDL_TEXTUREACCESS_STREAMING, W, H);
    if CanvasTex = nil then
    begin
      Writeln(StdErr, 'SDL_CreateTexture (canvas): ', SDL_GetError);
      SDL_DestroyRenderer(Renderer);
      SDL_DestroyWindow(Window);
      SDL_Quit;
      Bridge.Free;
      Halt(1);
    end;
    SDL_SetTextureScaleMode(CanvasTex, SDL_SCALEMODE_NEAREST);

    // Atari frame texture is created lazily on the first rawscreen
    // response, and re-created whenever the server starts reporting
    // a different size (overscan or region change).
    AtariTex  := nil;
    AtariTexW := 0;
    AtariTexH := 0;

    // --- Application state -----------------------------------------
    FillChar(Shadow[0], SizeOf(Shadow), 0);
    PaintColor   := 1;
    Dirty        := True;   // upload+refresh on first frame
    NeedRefresh  := True;
    MouseDown    := False;
    Running      := True;
    LastActivity := SDL_GetTicksNS;

    // Reusable TBytes wrapper around the fixed shadow array so we
    // can call MemLoadBytes. Length never changes.
    SetLength(ShadowBytes, SCREEN_LEN);

    // --- Main loop -------------------------------------------------
    while Running do
    begin
      // 1. Drain events.
      while SDL_PollEvent(@Ev) do
      begin
        case Ev.type_ of
        SDL_EVENT_QUIT:
          Running := False;

        SDL_EVENT_KEY_DOWN:
          case Ev.key.key of
            SDLK_ESCAPE: Running := False;
            SDLK_1:      PaintColor := 1;
            SDLK_2:      PaintColor := 2;
            SDLK_3:      PaintColor := 3;
            SDLK_C:
              begin
                FillChar(Shadow[0], SCREEN_LEN, 0);
                Dirty        := True;
                NeedRefresh  := True;
                LastActivity := SDL_GetTicksNS;
              end;
          end;

        SDL_EVENT_MOUSE_BUTTON_DOWN:
          begin
            if Ev.button.button <> SDL_BUTTON_LEFT then Continue;
            Fx := Ev.button.x;
            Fy := Ev.button.y;

            // Toolbar hit-tests first: a click on a widget selects
            // / clears instead of starting a paint stroke.
            Hit := False;
            for I := 0 to 2 do
              if InRect(Fx, Fy, SwatchX(I), TOOLBAR_Y,
                        SWATCH_W, SWATCH_H) then
              begin
                PaintColor   := I + 1;
                NeedRefresh  := True;
                LastActivity := SDL_GetTicksNS;
                Hit := True;
                Break;
              end;
            if (not Hit) and InRect(Fx, Fy, ClearX, TOOLBAR_Y,
                                    CLEAR_W, CLEAR_H) then
            begin
              FillChar(Shadow[0], SCREEN_LEN, 0);
              Dirty        := True;
              NeedRefresh  := True;
              LastActivity := SDL_GetTicksNS;
              Hit := True;
            end;
            if Hit then Continue;

            // Not on a widget — start painting. Paint the pixel
            // under the cursor immediately so a single click (no
            // drag) still leaves a mark.
            MouseDown := True;
            if (Fx >= 0) and (Fx < CANVAS_W)
               and (Fy >= 0) and (Fy < CANVAS_H) then
            begin
              PackPixel(Shadow, Trunc(Fx / SCALE),
                        Trunc(Fy / SCALE), PaintColor);
              Dirty        := True;
              NeedRefresh  := True;
              LastActivity := SDL_GetTicksNS;
            end;
          end;

        SDL_EVENT_MOUSE_MOTION:
          if MouseDown then
          begin
            Fx := Ev.motion.x;
            Fy := Ev.motion.y;
            if (Fx >= 0) and (Fx < CANVAS_W)
               and (Fy >= 0) and (Fy < CANVAS_H) then
            begin
              PackPixel(Shadow, Trunc(Fx / SCALE),
                        Trunc(Fy / SCALE), PaintColor);
              Dirty        := True;
              NeedRefresh  := True;
              LastActivity := SDL_GetTicksNS;
            end;
          end;

        SDL_EVENT_MOUSE_BUTTON_UP:
          if Ev.button.button = SDL_BUTTON_LEFT then
            MouseDown := False;
        end;
      end;

      // 2. Push shadow -> bridge if anything changed, then advance
      //    one frame and grab the live Atari output.
      if NeedRefresh then
      begin
        if Dirty then
        begin
          Move(Shadow[0], ShadowBytes[0], SCREEN_LEN);
          try
            Bridge.MemLoadBytes(SCREEN_ADDR, ShadowBytes);
          except
            on E: EBridgeError do
              Writeln(StdErr, 'memload: ', E.Message);
          end;
          Dirty := False;
        end;
        try
          Bridge.Frame(1);
        except
          on E: EBridgeError do
            Writeln(StdErr, 'frame: ', E.Message);
        end;
      end;

      if NeedRefresh then
      begin
        try
          Frame := Bridge.RawScreen;
        except
          on E: EBridgeError do
          begin
            Writeln(StdErr, 'rawscreen: ', E.Message);
            Frame.Width  := 0;
            Frame.Height := 0;
            Frame.Pixels := nil;
          end;
        end;
        if (Frame.Width > 0) and (Frame.Height > 0)
           and (Length(Frame.Pixels) = Frame.Width * Frame.Height * 4) then
        begin
          if (AtariTex = nil)
             or (Frame.Width <> AtariTexW)
             or (Frame.Height <> AtariTexH) then
          begin
            if AtariTex <> nil then SDL_DestroyTexture(AtariTex);
            AtariTex := SDL_CreateTexture(
              Renderer, SDL_PIXELFORMAT_XRGB8888,
              SDL_TEXTUREACCESS_STREAMING, Frame.Width, Frame.Height);
            if AtariTex <> nil then
            begin
              SDL_SetTextureScaleMode(AtariTex, SDL_SCALEMODE_NEAREST);
              AtariTexW := Frame.Width;
              AtariTexH := Frame.Height;
            end;
          end;
          if AtariTex <> nil then
            SDL_UpdateTexture(AtariTex, nil, @Frame.Pixels[0],
                              Frame.Width * 4);
        end;
      end;

      // 3. Render the local shadow into the canvas texture.
      ShadowToCanvasTexture(Shadow, CanvasPixels);
      SDL_UpdateTexture(CanvasTex, nil, @CanvasPixels[0], W * 4);

      // 4. Present.
      SDL_SetRenderDrawColor(Renderer, 32, 32, 32, 255);
      SDL_RenderClear(Renderer);

      CanvasDst.x := 0;
      CanvasDst.y := 0;
      CanvasDst.w := CANVAS_W;
      CanvasDst.h := CANVAS_H;
      SDL_RenderTexture(Renderer, CanvasTex, nil, @CanvasDst);

      // Letterbox the live frame into the reserved slot while
      // preserving its aspect ratio.
      if (AtariTex <> nil) and (AtariTexW > 0) and (AtariTexH > 0) then
      begin
        Sx := ATARI_SLOT_W / AtariTexW;
        Sy := ATARI_SLOT_H / AtariTexH;
        if Sx < Sy then S := Sx else S := Sy;
        DrawW := AtariTexW * S;
        DrawH := AtariTexH * S;
        AtariDst.x := CANVAS_W + GUTTER + (ATARI_SLOT_W - DrawW) * 0.5;
        AtariDst.y := (ATARI_SLOT_H - DrawH) * 0.5;
        AtariDst.w := DrawW;
        AtariDst.h := DrawH;
        SDL_RenderTexture(Renderer, AtariTex, nil, @AtariDst);
      end;

      // Toolbar: three colour swatches + clear button.
      for I := 0 to 2 do
      begin
        Rgb := CANVAS_RGB[I + 1];
        SDL_SetRenderDrawColor(Renderer,
          (Rgb shr 16) and $FF, (Rgb shr 8) and $FF, Rgb and $FF, 255);
        Swatch.x := SwatchX(I);
        Swatch.y := TOOLBAR_Y;
        Swatch.w := SWATCH_W;
        Swatch.h := SWATCH_H;
        SDL_RenderFillRect(Renderer, @Swatch);
        SDL_SetRenderDrawColor(Renderer, 200, 200, 200, 255);
        SDL_RenderRect(Renderer, @Swatch);
        if I + 1 = PaintColor then
        begin
          SDL_SetRenderDrawColor(Renderer, 255, 255, 0, 255);
          SDL_RenderRect(Renderer, @Swatch);
          Inner.x := Swatch.x + 1;
          Inner.y := Swatch.y + 1;
          Inner.w := Swatch.w - 2;
          Inner.h := Swatch.h - 2;
          SDL_RenderRect(Renderer, @Inner);
        end;
      end;

      // Clear button: dark grey fill + white border + centred "X"
      // drawn as two crossed lines (2 px thick).
      ClearRect.x := ClearX;
      ClearRect.y := TOOLBAR_Y;
      ClearRect.w := CLEAR_W;
      ClearRect.h := CLEAR_H;
      SDL_SetRenderDrawColor(Renderer, 64, 64, 64, 255);
      SDL_RenderFillRect(Renderer, @ClearRect);
      SDL_SetRenderDrawColor(Renderer, 220, 220, 220, 255);
      SDL_RenderRect(Renderer, @ClearRect);
      Pad := 8.0;
      X0  := ClearRect.x + Pad;
      Y0  := ClearRect.y + Pad;
      X1  := ClearRect.x + ClearRect.w - Pad;
      Y1  := ClearRect.y + ClearRect.h - Pad;
      for K := 0 to 1 do
      begin
        SDL_RenderLine(Renderer, X0 + K, Y0, X1 + K, Y1);
        SDL_RenderLine(Renderer, X1 + K, Y0, X0 + K, Y1);
      end;

      SDL_RenderPresent(Renderer);

      // 5. Decide whether the next iteration needs another bridge
      //    round-trip. Keep refreshing for GRACE_NS after the last
      //    paint / key event, then stop hitting the bridge until
      //    the user does something again. SDL_WaitEventTimeout
      //    blocks efficiently in the idle case.
      Now := SDL_GetTicksNS;
      if Now - LastActivity > GRACE_NS then
      begin
        NeedRefresh := False;
        if SDL_WaitEventTimeout(@Peek, 100) then
          SDL_PushEvent(@Peek);
      end
      else
      begin
        NeedRefresh := True;
      end;
    end;

    // --- Cleanup ---------------------------------------------------
    if AtariTex  <> nil then SDL_DestroyTexture(AtariTex);
    if CanvasTex <> nil then SDL_DestroyTexture(CanvasTex);
    if Renderer  <> nil then SDL_DestroyRenderer(Renderer);
    if Window    <> nil then SDL_DestroyWindow(Window);
    SDL_Quit;
  finally
    Bridge.Free;
  end;
end.
