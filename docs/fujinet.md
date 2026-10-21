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

## Run your first program through FujiNet

FujiNet-PC uses the computer's existing network connection. You do not need
hardware Wi-Fi provisioning, an SSID/password step, or a 2.4GHz adapter to
use the local runtime. Those instructions apply to a physical FujiNet.

### Browse a TNFS server

After **Start and Boot FujiNet** reaches CONFIG's **HOST LIST**:

1. Select an existing host with **1–8** or the arrow keys and press **Return**.
   `fujinet.online`, `apps.irata.online` and `fujinet.diller.org` were all
   verified through the real runtime. Their contents and paths can change;
   browse the host's Atari folders rather than relying on a fixed directory.
   To add a host, select an unused slot, press **E**, enter its hostname and
   confirm.
2. Use arrows and **Return** to enter folders and select a `.xex` program or
   bootable `.atr` image. **Escape** returns to the host list; **Backspace**
   goes up a directory in the file browser.
3. Choose drive **1**, press **Return**, then **Return** again for read-only
   access. Choose write mode only when the application needs it and the
   image is yours to modify.
4. Press Atari **OPTION** to mount the selected drives and boot. Its default
   PC binding is **F4**; use your configured console-key mapping if changed.
   The footer in the running CONFIG version shows the available controls.

A normal load-and-go XEX can be mounted directly: FujiNet presents a virtual
boot disk using its PicoBoot loader. You do not need to convert it to ATR or
boot a DOS disk first. Programs written for a particular DOS or extra hardware
still require that environment. Leave BASIC unchanged unless the program's
instructions require it disabled; then use **System → Internal BASIC (Boot
Without Option Key)** or hold Atari OPTION at the program's boot.

### Run a downloaded local XEX

1. Download a program from its author's site or another source you trust.
2. Open **Diagnostics → Open Data Folder**, enter its `SD/` directory, and
   copy the `.xex` there. This is the managed runtime's virtual SD card;
   copying into the extracted release's original `SD/` after initial setup
   does not update the private card.
3. In CONFIG choose the **SD** host, select the program, mount it on D1
   read-only and press Atari OPTION as above. The file is supplied over
   FujiNet's SIO connection.

For a FujiNet-specific first experiment, CONFIG also offers **L** for its
Game Lobby (confirm the prompt). This exercises FujiNet's own networking;
network features require software designed to use them. Adding FujiNet does
not make arbitrary games support online multiplayer.

## Guided setup and running a downloaded game

**Guided setup** in Tools → FujiNet is optional. It walks through choosing
Local PC or an existing connection, starting and booting, and browsing
software. Entering the guide does not change devices or start a process.
The usual panel remains available through **Full controls**.

Once the managed Local PC runtime is connected, choose **Run a file through
FujiNet...**. Select **Choose local game...**, or paste a direct HTTP(S)
URL and choose **Download, mount and boot**. AltirraSDL copies the game to
its private virtual SD card, finds the configured SD host, verifies that
the web interface serves this same SD directory, mounts D1 read-only using
FujiNet's API, checks the mount, then resets and resumes the Atari. XEX runs
through FujiNet's PicoBoot and real SIO; the emulator's program loader is
not used by this shortcut. Existing local images remain mounted.

The shortcut accepts individual XEX/COM/EXE/OBX and ATR/ATX images up to
64 MiB. ZIP collections belong in Game Library. Its **Internal BASIC**
control uses the current profile's existing setting: turn it off when a
game requires OPTION at boot, and retain it for software that needs BASIC.
A DOS-specific program still needs its required DOS/environment.

Cancellation never requests an automatic boot. If cancellation happens
after a mount request has begun, check FujiNet's web interface because
that remote request may already have completed. Profile/connection changes
also cancel automatic boot. This shortcut requires the managed runtime's
mount API; External and manual setups continue using CONFIG or their own
web interface. Their host slots, hub and process ownership are preserved.

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
the panel offers **Disconnect native NetSIO** before manual boot. Detection follows the descriptor actually loaded in the VM. Editing or
deleting its source with hot reload disabled preserves the manual route until
the device reloads; successful and failed reloads update detection. Other
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

**Boot Atari** disables the built-in D1 drive and Fast boot, selects
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

## Troubleshooting

- **Runtime does not start:** open **Diagnostics** and read the runtime log.
  Extract the complete official ATARI archive, choose the matching processor
  build, and install any runtime dependencies reported by the operating
  system. On Unix systems check that the selected executable is executable.
- **Waiting for a peer:** check BOIP host and UDP port, **Allow LAN**, and
  firewall rules. The local web port is a separate TCP service. A Python hub
  and native listener must not bind the same UDP port.
- **CONFIG does not boot:** first confirm the connection, resolve any full-drive
  D1 conflict, then use **Boot Atari**. Keep normal speed while using network
  software. The retained local D1 image does not mean its drive is enabled.
- **HTTPS requests fail:** inspect FujiNet's log, keep certificate assets from
  the complete release, and check the computer's clock and network/proxy
  access. Certificate verification is not bypassed.
- **Cannot change listener/runtime settings:** stop the managed process first.
  Configuration and boot controls are disabled during netplay.
- **Old manual script edited but mode unchanged:** recognition follows the
  custom-device VM's loaded source. With hot reload disabled, reload the
  custom device before expecting the panel to recognize the replacement.

The in-app guide is under **Help → Contents → AltirraSDL additions → FujiNet
setup and troubleshooting**. It is also linked from the FujiNet panel.

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

The managed GUI shortcut has a separate opt-in test:

```sh
python3 tests/integration/test_fujinet_managed.py \
  --emulator build/linux-release/src/AltirraSDL/AltirraSDL \
  --runtime /path/to/fujinet-pc-ATARI --keep
```

It runs a local XEX and enters a URL through the visible download dialog,
then checks distinct 6502 memory markers, actual runtime SIO reads and the
SD ownership probes. It uses isolated configuration and runtime copies,
preserves an existing local D1 image, and stops the managed process on exit.
Both cases passed on 2026-10-21 with the official runtime listed below.

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
The same legacy route also passed mounted-image reads through a separate
local TNFS daemon and the public TNFS, HTTP and HTTPS/JSON checks listed
above (17 checks in each native and legacy run).

- [Official FujiNet-PC ATARI builds](https://github.com/FujiNetWIFI/fujinet-firmware/releases/tag/nightly_fn_pc_v1.6.2-dev)
- [FujiNet emulator bridge and NetSIO protocol](https://github.com/FujiNetWIFI/fujinet-emulator-bridge)
- [FujiNet firmware's Atari NetSIO implementation](https://github.com/FujiNetWIFI/fujinet-firmware/blob/master/lib/bus/sio/NetSIO.cpp)
- [FujiNet documentation](https://fujinetwifi.github.io/fujinet-docs/)

Further setup background:

- [AtariAge: FujiNet-PC on LinuxMint with Altirra SDL](https://forums.atariage.com/topic/393713-fujinet-pc-on-linuxmint-waltirra-sdl/) — community troubleshooting of runtime builds and the manual hub route.
- [Original FujiNet-PC launcher installation guide](https://github.com/a8jan/fujinet-pc-launcher/blob/master/Install.md) — explains Fast boot, D1 conflicts and computer-only reset for the earlier manual arrangement.
- [Atari CONFIG keyboard implementation](https://github.com/FujiNetWIFI/fujinet-config/blob/master/src/atari/input.c) and [mount-and-boot implementation](https://github.com/FujiNetWIFI/fujinet-config/blob/master/src/atari/mount_and_boot.c).
- [FujiNet's Atari XEX media loader](https://github.com/FujiNetWIFI/fujinet-firmware/blob/master/lib/media/atari/diskTypeXex.cpp).
