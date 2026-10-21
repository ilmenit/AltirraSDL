# Android background recovery investigation

A Pixel 7 report described a black screen after leaving a running game in the
background for a while. No device was attached during this investigation, so the
exact device failure has not been reproduced.

The default Android display backend is SDL_GPU/Vulkan. Android builds previously
accepted any existing `android/SDL3` directory. CI's broad cache fallback could
therefore restore an older SDL despite `setup_sdl3.sh` specifying 3.4.14. The local
checkout was actually 3.4.4. In that version Vulkan surface loss could repeatedly
recreate the swapchain against the lost surface. SDL 3.4.14 also recreates the
surface. Setup now verifies the pinned commit on every scripted build, aligns
Java sources, and invalidates native caches when revisions change. Dirty source
checkouts are preserved and reported rather than overwritten.

SDL's default Android pause behavior can block inside event pumping before the
application receives its background events. The app now disables that blocking
and uses its own suspended loop, allowing main-thread input release and saves
before process termination. Suspend files are replaced atomically; their media
identity and actual pause state are persisted. Startup recovery clears the game
browser overlay and does not override an explicit command-line boot image.

## Remaining OpenGL fallback work

If Android replaces a lost EGL context, SDL emits `SDL_EVENT_RENDER_DEVICE_RESET`.
The optional OpenGL fallback currently has no complete handling for this event.
The display backend, ImGui renderer and independent UI texture caches retain old
object names. Calling ordinary destructors in the replacement context is unsafe:
stale names can refer to unrelated new objects, and the backend owns the obsolete
SDL context.

A complete fix needs explicit resource abandonment/recreation for the display,
ImGui font atlas, virtual keyboard, icons, emotes, library artwork, help/rewind
previews and debugger textures. CPU frame data and simulator/UI state must remain
intact. Changing activity recreation hints does not preserve a lost EGL context.
No partial GL reset handler is installed.

Physical validation should cover repeated app switching and screen lock during
running and manually paused games, both a live activity and Android process
termination, and forced EGL context replacement with the asset views open.
Check game pixels, controls, audio, fonts, artwork and preserved progress.
Desktop headless regressions cover suspend/foreground run state, process-loss
checkpoint recovery, temporary menu pauses and failed checkpoint replacement;
they do not simulate a mobile GPU context loss.
