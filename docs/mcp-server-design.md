# Embedded MCP server

The desktop frontend and `AltirraBridgeServer` expose the bridge command
layer through MCP Streamable HTTP. The implementation has no additional
dependencies and runs on the emulator's main thread. It is compiled with
`ALTIRRA_MCP=ON` (the desktop default), requires `ALTIRRA_BRIDGE`, and is
disabled for Android and WebAssembly.

## Configuration

In AltirraSDL, open **System > Configure System > Emulator > MCP Server**.
The server is disabled until **Enabled** is selected. The port defaults to
7750. Changes apply immediately, and settings persist machine-wide under
the `MCP Server` registry key, independently of emulation profiles.

The headless server uses command-line configuration instead:

```sh
AltirraBridgeServer --mcp=7750 --mcp-token=your-access-token
```

`--mcp` uses port 7750; `--mcp-token` is optional and requires `--mcp`.
A bind failure stops startup with an error. The bridge socket remains
available alongside MCP. Connect an MCP Streamable HTTP client to
`http://127.0.0.1:7750/mcp`; when a token is required, configure its
`Authorization: Bearer <token>` header.

## Protocol and execution

The endpoint supports protocol version `2026-07-28` and legacy versions
`2025-11-25`, `2025-06-18`, and `2025-03-26`. Its tool catalogue and resource
schemas are discoverable through MCP. Legacy session initialization and
notification streams are supported, along with modern stateless requests
and subscription streams. Unauthenticated browser origins and non-loopback
Host values are rejected.

Frame runs, cycle stepping, and bare boots share the bridge's automation
gate. Commands from another frontend wait until the current operation
finishes, preserving the stopped-machine inspection boundary. MCP and the
socket frontend own separate injected-input contexts; releasing one
context does not release keys, modifiers, joystick buttons, or console
switches held by the other. MCP requests share one input context. Stopping
the MCP server releases its input and cancels its outstanding work.

## Security and resource limits

The listener binds only to IPv4 loopback. Host and Origin checks protect
against browser cross-origin requests and DNS rebinding. Authentication is
optional and off by default. Access grants control of emulation, memory,
debugging, and the file operations exposed by the tools; tool annotations
describe those effects and do not enforce access restrictions.

There are at most eight connections, 16 KiB of headers, 1 MiB per request
body, 64 levels of JSON nesting, and 16 MiB of queued output per connection.
Backpressure stops dispatching further requests on that connection. Idle
connections and stalled output expire after 60 seconds; active parked
requests can remain open. Accept and request work is bounded per poll.

`mcp-http-transport` tests real loopback backpressure and cancellation.
The bridge regression suite covers the emulator-facing integration.
