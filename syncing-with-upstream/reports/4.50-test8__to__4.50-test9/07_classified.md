# Three-way files by role and module

Report dir: `4.50-test8__to__4.50-test9`

- Three-way files: **43**
- Added in NEW:    **4**
- Removed in NEW:  **0**
- Trivial copies:  **166** (see `04_trivial_copy.txt`)

## port

**Port:** apply upstream changes into the fork (platform-agnostic).

### altirra-core  (10)

- `src/Altirra/source/console.cpp`  _MODIFIED_ — Platform-agnostic Altirra sources
- `src/Altirra/source/debugger.cpp`  _MODIFIED_ — Platform-agnostic Altirra sources
- `src/Altirra/source/idevhdimage.cpp`  _MODIFIED_ — Platform-agnostic Altirra sources
- `src/Altirra/source/printerexport.cpp`  _ADDED_ — Platform-agnostic Altirra sources
- `src/Altirra/source/printerrasterizer.cpp`  _ADDED_ — Platform-agnostic Altirra sources
- `src/Altirra/source/simulator.cpp`  _MODIFIED_ — Platform-agnostic Altirra sources
- `src/Altirra/source/siomanager.cpp`  _MODIFIED_ — Platform-agnostic Altirra sources
- `src/Altirra/source/trace.cpp`  _MODIFIED_ — Platform-agnostic Altirra sources
- `src/Altirra/source/tracefileencoding.cpp`  _MODIFIED_ — Platform-agnostic Altirra sources
- `src/Altirra/source/tracetape.cpp`  _MODIFIED_ — Platform-agnostic Altirra sources

### altirra-headers  (4)

- `src/Altirra/h/gtiarenderer_neon.inl`  _MODIFIED_
- `src/Altirra/h/printerexport.h`  _ADDED_
- `src/Altirra/h/printerrasterizer.h`  _ADDED_
- `src/Altirra/h/stdafx.h`  _MODIFIED_

### core-io  (3)

- `src/ATIO/source/audioreaderflac.cpp`  _MODIFIED_
- `src/ATIO/source/cassetteaudiofilters.cpp`  _MODIFIED_
- `src/ATIO/source/vorbisbitreader.cpp`  _MODIFIED_

### core-utils  (4)

- `src/ATCore/source/checksum_intrin.cpp`  _MODIFIED_
- `src/ATCore/source/configvar.cpp`  _MODIFIED_
- `src/ATCore/source/decmath.cpp`  _MODIFIED_
- `src/ATCore/source/fft.cpp`  _MODIFIED_

### headers-at  (1)

- `src/h/at/atcore/fft.h`  _MODIFIED_

### headers-vd2-system  (11)

- `src/h/vd2/system/atomic.h`  _MODIFIED_
- `src/h/vd2/system/binary.h`  _MODIFIED_
- `src/h/vd2/system/bitmath.h`  _MODIFIED_
- `src/h/vd2/system/date.h`  _MODIFIED_
- `src/h/vd2/system/file.h`  _MODIFIED_
- `src/h/vd2/system/int128.h`  _MODIFIED_
- `src/h/vd2/system/thread.h`  _MODIFIED_
- `src/h/vd2/system/thunk.h`  _MODIFIED_
- `src/h/vd2/system/vdalloc.h`  _MODIFIED_
- `src/h/vd2/system/vdtypes.h`  _MODIFIED_
- `src/h/vd2/system/zip.h`  _MODIFIED_

### kasumi  (3)

- `src/Kasumi/h/resample_stages_x64.h`  _MODIFIED_
- `src/Kasumi/source/resample_stages_x64.cpp`  _MODIFIED_
- `src/Kasumi/source/uberblit_resample.cpp`  _MODIFIED_

### system  (3)

- `src/system/source/cpuaccel.cpp`  _MODIFIED_
- `src/system/source/math.cpp`  _MODIFIED_
- `src/system/source/thread.cpp`  _MODIFIED_

## reflect-in-ui

**Reflect in UI:** understand change, mirror user-visible behaviour in Dear ImGui frontend.

### cmd  (2)

- `src/Altirra/source/cmdcassette.cpp`  _MODIFIED_ — UI command handler; reflect user-visible changes in ImGui
- `src/Altirra/source/cmddebug.cpp`  _MODIFIED_ — UI command handler; reflect user-visible changes in ImGui

## copy-verbatim

**Copy verbatim:** upstream-authoritative; just copy.

### docs  (2)

- `src/Altirra/res/changes.txt`  _MODIFIED_
- `src/Altirra/res/romset.html`  _MODIFIED_

## port-if-wired

**Port-if-wired:** port when the corresponding target exists in AltirraSDL.

### tests  (2)

- `src/ATTest/source/TestHLE_FPAccel.cpp`  _MODIFIED_
- `src/ATTest/source/TestIO_Vorbis.cpp`  _MODIFIED_

## skip-win-only

**Skip:** Win32-only path, not built on SDL — but read for context.

### altirra-res  (2)

- `src/Altirra/res/Altirra.rc`  _MODIFIED_ — Win32 .rc / resource headers
- `src/Altirra/res/resource.h`  _MODIFIED_ — Win32 .rc / resource headers

