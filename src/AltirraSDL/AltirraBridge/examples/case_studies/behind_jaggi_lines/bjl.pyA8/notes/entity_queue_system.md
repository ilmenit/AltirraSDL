# Entity & Command Queue System

The game uses a central command queue and entity management system that ties
together game logic, rendering, sound, and state updates. This is the highest
fan-in subsystem in the entire program.

## Command Queue

| Address | Name                 | Callers | Role |
|---------|----------------------|---------|------|
| $9290   | queue_push           | 41      | Push command to circular buffer at $071B, 32 slots |
| $94AA   | queue_push_flagged   | 45      | Push with bit 7 set — flagged/priority event |

The queue is a **circular buffer** at $071B with 32 slots. It is the primary
mechanism for inter-subsystem communication: rendering, sound, input, and game
logic all push events here.

Combined, these two routines are called from **86 call sites** — by far the
most-referenced code in the entire game. Nearly every subsystem pushes events
to the queue.

## Entity Management

| Address | Name              | Callers | Role |
|---------|-------------------|---------|------|
| $94B1   | entity_load_state | 13      | Load entity state block from ROM tables into work area |
| $92AA   | entity_swap       | 11      | Swap/deactivate game objects in entity array |

Entity state is organized as parallel arrays in the $0600-$07FF region:
- $066D,X — entity type/flags (4-bit type in low nibble)
- $067B,X — entity state/animation
- $0707,X — entity active flag (0 = inactive)
- $071B,X — command queue slots (shared region)

`entity_load_state` reads entity templates from ROM tables and populates the
work area. `entity_swap` manages the active entity list — deactivating objects
and compacting the array.

## Score System

BCD (Binary-Coded Decimal) arithmetic is used for score display:

| Address | Name                       | Role |
|---------|----------------------------|------|
| $4C91   | math_update_score_4C91     | Score computation with BCD add |
| $64ED   | game_logic_update_score    | Score update entry — BCD add + display |
| $655E   | game_logic_bcd_add_655E    | Lookup pilot map ($0A00) + BCD increment |
| $6561   | game_logic_bcd_add_6561    | Set flags at $0638/$0639 + BCD add |
| $656B   | game_logic_bcd_add_656B    | Core BCD add: SED / ADC $01 / CLD on $063F |
| $7DF6   | timing_update_score_7DF6   | Score display timing + BCD |

Score storage at $063F uses BCD encoding (SED/CLD bracketed ADC).
Flags at $0638/$0639 track scoring events.

## Random Number Generation

| Address | Name                       | Role |
|---------|----------------------------|------|
| $5F75   | random_select              | Weighted random select: reads POKEY RANDOM, indexes table |
| $5C69   | util_random_perturb_5C69   | Random perturbation of a value using RANDOM |
| $5BF2   | render_random_perturb_5BF2 | Random perturbation during rendering |
| $6539   | game_logic_get_random_6539 | Random value for game logic decisions |
| $8E0B   | render_random_perturb_8E0B | Random perturbation during terrain render |

`random_select` ($5F75) is called from **16 sites** — it reads POKEY RANDOM
($D20A) and selects from a weighted table. Used for terrain variation, entity
placement, and game event probability.

## Pilot Detection

| Address | Name         | Role |
|---------|--------------|------|
| $A690   | detect_pilot | Scan terrain for downed pilot — check pilot_map ($0A00) vs viewer |
| $6555   | game_logic_table_lookup_6555 | Index into pilot location buffer at $0A00 |

The pilot map at $0A00-$0AFF (256 bytes) tracks pilot locations across the
terrain grid. `detect_pilot` compares the viewer position against this map to
determine if a pilot is within rescue range.
