# Sound Engine Architecture

Behind Jaggi Lines uses a multi-channel POKEY sound system with a queue-driven
update model. All 4 POKEY channels (AUDF1-4 / AUDC1-4) are used.

## Entry Points

| Address | Name                | Role |
|---------|---------------------|------|
| $90BD   | sound_init          | Zero all 4 POKEY channels, clear state |
| $68BF   | sound_engine_update | Per-frame tick — process sound effect queue, update envelopes |
| $622F   | sound_play_effect   | Play a sound effect by ID — set frequency/control for channel |
| $91E7   | sound_set_channel   | Low-level: write AUDF/AUDC for a specific channel pair |
| $9119   | sound_dispatch      | Dispatch indexed sound command via table lookup |

## Sound Effects

Each effect is a named routine that configures one or more POKEY channels:

| Address | Name            | Channels | Description |
|---------|-----------------|----------|-------------|
| $9510   | sound_thrust    | AUDCTL   | Thrust engine continuous tone |
| $9521   | sound_explosion | Ch 1-3   | Explosion — multi-channel noise burst, calls wait_2 for decay |
| $9551   | sound_laser     | Ch 1-4   | Laser fire — all 4 channels, short burst |
| $9568   | sound_warning   | Ch 1-4   | Warning alarm — AUDF1-4 cycled for siren effect |
| $9650   | sound_pickup    | AUDCTL   | Pilot pickup confirmation tone |
| $9661   | sound_landing   | AUDCTL   | Landing approach guidance tone |
| $966E   | sound_ambient   | Multiple | Ambient wind/environment — fill loop sets base frequencies |

## Architecture

- `sound_engine_update` ($68BF) is called from the deferred VBI handler
- It polls RTCLOK for frame-sync timing to drive envelope updates
- Sound effects write directly to POKEY registers (not shadow) for immediate response
- `sound_init` fills all AUDF/AUDC registers with zero using an indexed fill loop
- `sound_dispatch` ($9119) uses a BIT-skip trick (flagged as anomaly) to branch
  between sound command types

## POKEY Register Usage

- AUDF1/AUDC1 ($D200/$D201): Primary channel — laser, warning fundamentals
- AUDF2/AUDC2 ($D202/$D203): Secondary channel — harmonics, explosion body
- AUDF3/AUDC3 ($D204/$D205): Tertiary channel — overtones, ambient
- AUDF4/AUDC4 ($D206/$D207): Fourth channel — noise components
- AUDCTL ($D208): Audio control — clock selection, filter configuration
- RANDOM ($D20A): Read for pseudo-random noise generation in ambient effects
