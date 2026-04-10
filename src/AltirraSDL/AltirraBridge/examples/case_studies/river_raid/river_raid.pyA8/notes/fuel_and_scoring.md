# River Raid — Fuel System & Scoring

## Fuel System

### Fuel Variables

| Variable | Purpose |
|----------|---------|
| $76 | **fuel_level**: Current fuel gauge value ($FF=full, $00=empty) |
| $32 | **fuel_drain_ctr**: Drain rate counter (decremented per frame) |
| $38 | **fuel_state**: Fuel tank state; bit 7 = currently refueling at depot |

### Fuel Drain

Fuel drains continuously during gameplay:

1. Each frame, $32 (fuel_drain_ctr) is decremented at $AE94
2. The drain rate varies with speed — flying faster (joystick up)
   drains fuel faster
3. When $32 wraps, $76 (fuel_level) is decremented
4. When $76 reaches $00, the player dies (fuel exhaustion)

The drain rate is linked to the scroll speed: faster scrolling =
faster fuel consumption. This creates a strategic tension between
speed (to cover ground and dodge enemies) and fuel conservation.

### Fuel Refill

Fuel depots are entity type $07. When the player's sprite overlaps
a fuel depot:

1. The overlap is detected in the main loop collision handler
2. $38 is set with bit 7 ($38 |= $80) at $A515
3. While $38 bit 7 is set, $76 is incremented each frame (refueling)
4. $76 caps at $FF (tank full)
5. If the player fires at the depot, it's destroyed and refueling stops

### Fuel Gauge Display

The fuel gauge is drawn in the status bar at $3D80+ by the routine
at $B183 (update_fuel_gauge). It consists of colored blocks:

- The number of filled blocks corresponds to $76 / some divisor
- Colors change based on level: green (>50%), yellow (25-50%), red (<25%)
- The display update pointer table at $BAB7 maps fuel levels to
  screen positions

## Scoring System

### Score Storage

The score is stored as 6 BCD digits across 3 bytes:

| Byte | Digits | Range |
|------|--------|-------|
| $63 | Hundred-thousands / millions | $00-$99 |
| $64 | Thousands / ten-thousands | $00-$99 |
| $65 | Ones / tens / hundreds | $00-$99 |

Maximum displayable score: 999,999 (which wraps to 000,000).

### Score Arithmetic

The scoring routine at $A547 uses 6502 decimal mode:

```
$A547: SED                    ; enable decimal mode
$A548: LDA $64                ; load mid-score byte
$A54A: AND #$F0               ; save tens digit
$A54C: STA $B1                ; store for comparison
$A54E: LDA $BB0B,Y            ; load score value for this entity type
$A551: CLC
$A552: ADC $65                ; add to low score
$A554: STA $65
$A556: LDA $64
$A558: ADC #$00               ; propagate carry
$A55A: STA $64
$A55C: LDA $63
$A55E: ADC #$00               ; propagate carry to high
$A560: STA $63
$A562: CLD                    ; disable decimal mode
```

### Score Values Per Entity

The score value table at $BB0B is indexed by the entity's shape/type
value. The exact BCD values per entity type (to be confirmed with
runtime observation):

| Entity | Estimated Points |
|--------|-----------------|
| Ship | 30 |
| Helicopter | 60 |
| Jet | 100 |
| Balloon | 60 |
| Bridge | 500 |
| Fuel depot | 80 (if destroyed — but you shouldn't!) |

### Score Display

The score is displayed in the status bar center area. The display
update routine at $B194 converts the BCD bytes to screen characters
and writes them to the status bar screen RAM.

The high score for the session is maintained separately and displayed
to the right of the current score ("HI" label).

### Extra Lives

Extra lives are awarded at score thresholds:

1. The variable $6A (extra_life_thresh) tracks progress toward the
   next extra life
2. After each score update, $A574-$A589 checks if the ten-thousands
   digit changed (by comparing $64 AND $F0 before and after)
3. If it changed: increment $6A
4. At certain $6A values: award an extra life
5. The extra life handler at $A436 increments $2A (lives_count),
   plays a sound, and updates the lives display

The threshold pattern is approximately every 10,000 points, but
becomes less frequent at higher scores (the exact intervals depend
on $6A's threshold comparison).
