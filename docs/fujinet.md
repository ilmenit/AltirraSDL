# FujiNet and native NetSIO

AltirraSDL can connect its emulated SIO bus directly to FujiNet-PC or a
FujiNet using the UDP NetSIO protocol. The connection does not need the
Python hub or an Altirra custom-device descriptor. Open **Tools →
FujiNet** for connection, boot, runtime and diagnostic controls.

## Local FujiNet-PC

1. Select **Local PC**. Use **Download FujiNet-PC** to open the official releases. Download
   the **ATARI** archive matching your operating system and processor,
   then extract the complete archive.
2. Use **Choose FujiNet-PC** to select the extracted `fujinet` executable
   (`fujinet.exe` on Windows). Keep `data/`, `SD/` and `fnconfig.ini`
   beside it. Select the executable itself, rather than `run-fujinet`.
3. Select **Start and Boot FujiNet**. AltirraSDL starts the listener and
   process, waits for their connection, then boots the Atari.
4. Use **Open Web Interface** or FujiNet's on-screen configuration program
   to mount software, then **Boot Atari**. **Start without booting** starts
   only the runtime and leaves the Atari's state unchanged.

The main view shows connection state, the selected runtime and the actions
available now. **Runtime settings**, **Connection settings**, **Local disks**
and **Diagnostics** hold optional controls. Installation instructions appear
under **Installation help** before a runtime has been selected. The dialog
adapts its height to the current view, keeps its position inside the canvas,
and wraps actions when using narrow windows or large fonts.

The launcher passes argument arrays directly through SDL's process API.
It supports paths containing spaces and keeps its configuration, virtual
SD card and logs in `<Altirra configuration directory>/FujiNet/`. The
distribution's configuration is copied on first use; subsequent launches
preserve the private host slots and other FujiNet settings. **Open Data
Folder** opens this directory. The runtime executable and web port are
machine-global preferences; the NetSIO device configuration belongs to
the active emulator profile.

FujiNet-PC's `-u` argument is its **web server URL**, not its SIO endpoint.
The managed launcher sets the private configuration's `[BOIP]` host and
port to the local UDP listener. FujiNet-PC's expected exit code 75 causes
an automatic restart; repeated restart requests are bounded and reported.
An operating-system lock prevents another managed runtime from modifying
the same private data directory concurrently. Closing AltirraSDL stops
only the process it started. Output is drained
continuously, with a bounded log and an in-panel log tail.

## Existing process or external FujiNet

Select **External**, then **Connect NetSIO**. The default listener is `127.0.0.1:9997`.
Configure an independently running FujiNet-PC to send NetSIO to that
address. For a FujiNet on the network, enable **Allow LAN**, then
configure FujiNet's SIO-over-network settings with this
computer's LAN address and the selected UDP port. **Connection settings**
holds the LAN choice, port, timing and optional web URL. Allow that port in
the host firewall when needed.

AltirraSDL listens; FujiNet initiates the connection. The panel displays
the connected peer, packet counters, synchronization timeouts and errors.
**Apply connection settings** discards the previous transaction and waits for a
fresh connection. **Disconnect** stops the listener. An external web
interface URL can be saved independently of the local runtime's web port.
**Reconnect NetSIO** restores a disconnected listener while keeping a
managed runtime running and leaving the Atari's state unchanged. While
the local runtime is running, **Open Web Interface** opens its local web
port; once it stops, the button uses the saved external URL if present.

An already connected external peer is preserved. Opening the panel selects
the External view; trying to start a second managed runtime is rejected
before reconnecting, resetting the Atari or launching a process. Stopping a
managed runtime clears its old peer while retaining a healthy listener, so
an immediate managed restart does not mistake stale peer state for an
externally owned runtime.

## Existing custom-device / Python-hub setup

The earlier manual route remains supported:

`Atari SIO → NetSIO custom .atdevice → TCP hub → UDP → FujiNet`

The panel recognizes the published NetSIO descriptor, including renamed
copies, and displays **Manual FujiNet connection**. It does not replace the
descriptor, alter its hot-reload/unsafe options, start the hub, or take
ownership of your existing FujiNet process. **Boot Atari** resets only the
computer; the custom-device VM and hub remain running. The optional web URL
and local-drive controls remain available.

Native activation is blocked while a recognized legacy NetSIO custom device
is configured. Running both routes can duplicate SIO traffic; the hub and
native listener also both default to UDP 9997. If both are already present,
the panel offers **Disconnect native NetSIO** before manual boot. Descriptor
edits invalidate detection, including in-place hot-reload updates. Other
custom devices are left usable. Partially recognizable descriptors produce
a caution; detection cannot prove the purpose of arbitrary user scripts.

Choose the arrangement that matches your setup:

| Arrangement | Transport | Process ownership |
| --- | --- | --- |
| Local PC | Native UDP NetSIO | AltirraSDL starts/stops one selected FujiNet-PC |
| External | Native UDP NetSIO | You start and configure FujiNet or FujiNet-PC |
| Existing manual bridge | Custom device and Python hub | You retain the descriptor, hub and runtime |

Transport selection stays separate from process ownership. Automatic
migration would lose hub fan-out and user-edited filters, so it is not used.
To switch to native NetSIO, remove the NetSIO custom device from that profile
and stop the hub first. Separate profiles let you retain both arrangements
without routing one Atari's SIO traffic through both at once. Multiple native
NetSIO devices also require selecting one in Configure System before the
panel will reconfigure or boot them.

Native Android builds can connect to an external FujiNet. Local desktop
process management is unavailable there. Browsers cannot accept native
UDP NetSIO connections; the panel explains this limitation.

## Boot and local media

**Boot FujiNet** disables the built-in D1 drive and Fast boot, selects
normal 100% speed, resets the computer while leaving FujiNet powered,
and resumes execution. WAN operations need wall-clock time; Warp speed
can otherwise outrun Atari OS network timeouts. BASIC stays unchanged. The D1
image and unsaved changes remain mounted. **Enable local D1** makes that
drive available again. Other local drives keep their settings and media.
Assign different drive numbers to active local and FujiNet drives.

A full-drive emulator attached to D1 must be disabled explicitly in
Configure System before FujiNet boot. The panel reports this conflict
instead of removing hardware. Configuration and boot controls are
disabled during an active netplay session.

## Transport and save states

The implementation forwards command, motor, data, baud, PROCEED and
INTERRUPT events. Incoming data is paced through the emulated serial bus.
Command and write acknowledgements use NetSIO synchronization, with a
configurable bounded wall-clock timeout. Packet credits bound the receive
queue; transmit backpressure waits are bounded as well. Worker callbacks
queue data and notifications; only the emulation thread changes hardware.

Host-to-FujiNet data uses individual DATA_BYTE messages. This avoids the
released firmware's DATA_BLOCK trailing-byte quirk without relying on
that quirk in newer peers. Incoming DATA_BLOCK messages retain every
payload byte. The message IDs follow the firmware header; the older
protocol document swaps PROCEED and INTERRUPT in its prose.

Save states preserve the device configuration and clear pending local
transactions when restored. They report partial accuracy: external
network connections and FujiNet's filesystem state cannot be rewound.

## Verification and references

The `netsio-protocol` CTest runs real local UDP sockets against a mock
peer, including timing, acknowledgements, credits, burst transfers,
malformed packets, peer filtering and cleanup. `fujinet-pc-process`
exercises process arguments, private configuration and SD data, logs,
restart limits and shutdown on the native CI platforms.

The opt-in live test uses an actual FujiNet-PC distribution and executes
Atari OS SIO calls inside the emulator:

```sh
python3 tests/integration/test_fujinet_pc.py \
  --bridge build/linux-release/src/AltirraBridgeServer/AltirraBridgeServer \
  --runtime /path/to/fujinet-pc-ATARI \
  --tnfsd /path/to/tnfsd --public --keep
```

Add `--legacy-bridge /path/to/fujinet-emulator-bridge` to exercise the
published custom descriptor and Python hub instead of native NetSIO. This
uses a private renamed descriptor, isolated TCP/UDP ports, and the same
Atari OS SIO tests. It never modifies your bridge checkout. Both routes
support the optional public-service and TNFS checks.

Local fixtures remain separate from public-service availability. The
test retains a machine-readable report, runtime logs and a configuration
screen capture. See the script's help for required tools and options.

On 2026-10-21, the live test passed using the official Atari FujiNet-PC
v1.6-eaa520061 build dated 2026-10-03. It booted the actual CONFIG host
screen with BASIC both enabled and disabled, read host slots through
SIOV, preserved local D2 alongside FujiNet D1, read a mounted TNFS image,
and completed local N: HTTP and JSON requests. With the SIO patch enabled,
local D2 reads remained accelerated (no external SIO disk command), while
FujiNet control, N: HTTP/JSON and TNFS transactions continued to work.

Native runtime disconnect and restart recovery also passed. Public SIO transactions listed directories
on `fujinet.online`, `apps.irata.online` and `fujinet.diller.org`, retrieved
`http://fujinet.online/`, and parsed `/name` from the official HTTPS board
JSON on `raw.githubusercontent.com` as `#FujiNet v1`. The bridge runs with
`--pacing=realtime` for these checks: WAN operations require wall-clock
time while Atari OS timeouts use emulated time. Public-service results
remain dependent on those services and the test machine's network.

The original manual descriptor/hub route also passed CONFIG boot, control
commands, accelerated local D2 coexistence, local N: HTTP/JSON, SD directory
and sector reads, and runtime disconnect/reconnect with the official runtime.
That legacy verification used local fixtures; public WAN and a separate TNFS
daemon were not part of the legacy run.

- [Official FujiNet-PC ATARI builds](https://github.com/FujiNetWIFI/fujinet-firmware/releases/tag/nightly_fn_pc_v1.6.2-dev)
- [FujiNet emulator bridge and NetSIO protocol](https://github.com/FujiNetWIFI/fujinet-emulator-bridge)
- [FujiNet firmware's Atari NetSIO implementation](https://github.com/FujiNetWIFI/fujinet-firmware/blob/master/lib/bus/sio/NetSIO.cpp)
- [FujiNet documentation](https://fujinetwifi.github.io/fujinet-docs/)
