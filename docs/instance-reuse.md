# Desktop program instance reuse

Configure System → UI → Reuse program instance forwards subsequent
launches to the existing desktop instance. The option is machine-global and
uses the native `Settings` key `Startup: Reuse program instance`.

`--singleinstance` (alias `--si`) enables reuse for one launch.
`--nosingleinstance` (alias `--nosi`) starts an independent instance.
When both are supplied, the enabling switch takes precedence, matching Windows
Altirra. These switches do not change the saved preference.

The receiver processes requests on its main thread. Media, firmware, cheat-file,
PCLink and H: paths resolve against the sender's working directory. Nested
ZIP/GZIP/Atari filesystem paths are supported. Forwarded debugger commands
inspect the current emulation state without a startup reset or rerunning
`startup.atdbg`. Paths embedded inside debugger command strings retain the
debugger's normal path semantics; use absolute paths there when launching from
another working directory.

Requests wait while Online Play is engaged or the native macOS menu is being
tracked. Invalid launch options report an error in the existing instance.
Window activation is requested through SDL; the window manager may deny focus
according to its activation policy.

Test mode, headless/offscreen sessions, bridge sessions, Android and WebAssembly
remain independent even when reuse is requested. On Windows, elevated launches
also remain independent and neither publish nor forward instance IPC. This
prevents a non-elevated process belonging to the same user from controlling an
elevated emulator. If Windows cannot determine the process elevation, the
launch likewise continues independently.

The endpoint uses a loopback socket and a random capability stored in a private
per-user directory (`~/.altirra-instance` on POSIX;
`%LOCALAPPDATA%/AltirraSDL/instance` on Windows). An operating-system file lock
serializes startup and releases automatically after a crash. If an owner exits
during hand-off, the waiting launch becomes its replacement. The lock file
remains in place to avoid races between different lock-file inodes.

## Validation

The CMake target `altirra-instance-reuse-test-fixture` and CTest `instance-reuse`
exercise process election, argument transport, disabling reuse, crash recovery,
owner shutdown during hand-off, and malformed/partial requests. Elevated Windows
runners instead verify independent launches and skip normal IPC tests.

Run the additional real desktop test with:

```sh
PYTHONDONTWRITEBYTECODE=1 python3 tests/test_instance_reuse_desktop.py \
    build/linux-release/src/AltirraSDL/AltirraSDL -v
```

It uses isolated POSIX settings, loads a Unicode relative XEX in an ordinary
desktop primary, and checks its RAM through a subsequent forwarded debugger
command. It also checks that invalid options leave the primary running and
that `--nosi` and `--test-mode --si` launch independent processes. Linux/X11
foreground verification reports compositor policy limitations; set
`ALTIRRA_TEST_REQUIRE_FOCUS=1` under a controllable window manager to require
successful focus activation. The real desktop test skips Windows because its
registry settings cannot be isolated through `HOME` and an INI file.
