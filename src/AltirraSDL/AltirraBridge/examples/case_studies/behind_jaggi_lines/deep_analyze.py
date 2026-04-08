#!/usr/bin/env python3
"""Deep analysis of Behind Jaggi Lines (Rescue on Fractalus!) using pyA8.

Generic analysis is handled by proj.deep_analyze().
Game-specific knowledge (labels, comments, data regions) is here.
"""
import sys, os
sys.path.insert(0, '/home/ilm/Documents/GitHub')

from pyA8 import Atari, Project

GAME = '/home/ilm/Documents/GitHub/pyA8/examples/games/behind_jaggi_lines/behind_jaggi_lines.xex'
PROJ_DIR = '/home/ilm/Documents/GitHub/pyA8/examples/games/behind_jaggi_lines/bjl.pyA8'

proj = Project.open(PROJ_DIR)

# Ensure memory snapshot
if not proj._memory_valid:
    print("Booting emulator for memory snapshot...")
    emu = Atari(GAME)
    proj.attach(emu)
    proj.snapshot()
    emu.quit()

# Run full analysis pipeline (includes call graph)
print(proj.deep_analyze())

# ═══════════════════════════════════════════════════════════
# Game-specific labels — human understanding of Rescue on Fractalus!
# ═══════════════════════════════════════════════════════════

# ── Main entry / hardware init ──
proj.label(0x3C00, 'game_start', type='entry_point',
           comment='Main entry — hardware init, VBI/IRQ setup, title screen')

# ── Interrupt handlers ──
proj.label(0x8059, 'gameplay_vbi', type='vector',
           comment='Gameplay VBI — colors, PMG, DMA, rendering dispatch')
proj.label(0x840A, 'vbi_title_screen', type='vector',
           comment='Title screen VBI — attract mode, color cycling')
proj.label(0x82AA, 'vbi_deferred', type='subroutine',
           comment='Deferred VBI — game state updates, collision checks')
proj.label(0x4940, 'irq_handler', type='irq',
           comment='Custom IRQ — keyboard via KBCODE, IRQEN management')

# ── DLI chain ──
proj.label(0x4CF1, 'dli_dispatch', type='vector',
           comment='DLI dispatcher — indexes jump table by stage counter $C8')
proj.label(0x4D0D, 'dli_sky', type='subroutine', comment='DLI: sky colors')
proj.label(0x4D20, 'dli_horizon', type='subroutine',
           comment='DLI: sky-to-terrain transition, PRIOR=$11')
proj.label(0x4D41, 'dli_ground', type='subroutine', comment='DLI: terrain ground colors')
proj.label(0x4D79, 'dli_hud', type='subroutine', comment='DLI: HUD transition')
proj.label(0x610B, 'dli_colors_a', type='subroutine',
           comment='DLI: load color regs from shadow, set PRIOR=$94')
proj.label(0x6184, 'dli_colors_b', type='subroutine',
           comment='DLI: set terrain/ground color registers')
proj.label(0x61CF, 'dli_colors_c', type='subroutine',
           comment='DLI: set PRIOR, load colors for cockpit section')

# ── Terrain engine ──
proj.label(0x843C, 'terrain_init', type='subroutine',
           comment='Fractal terrain init — fills height buffer, starts midpoint displacement')
proj.label(0x84E6, 'terrain_subdivide', type='subroutine',
           comment='Terrain midpoint displacement step — 4 cardinal neighbors')
proj.label(0x84C4, 'terrain_check_neighbors', type='subroutine',
           comment='L-shaped neighbor check — returns negative if all unresolved')
proj.label(0x8526, 'terrain_push_seed', type='subroutine',
           comment='Push current seed to recursion stack, increment depth')
proj.label(0x5C88, 'terrain_init_strips', type='subroutine',
           comment='Initialize terrain strip rendering buffers')
proj.label(0xA356, 'terrain_render', type='subroutine',
           comment='Main terrain rendering — iterate visible columns, transform + fill')

# ── 3D engine ──
proj.label(0x9CCF, 'mul16_signed', type='subroutine', comment='16-bit signed multiply')
proj.label(0x9DA7, 'div16', type='subroutine', comment='16-bit divide')
proj.label(0x9E8C, 'transform_vertex', type='subroutine',
           comment='3D vertex transform — rotation + translation through camera matrix')
proj.label(0xA0DB, 'compute_screen_offset', type='subroutine',
           comment='Screen buffer offsets for terrain columns from viewer position')
proj.label(0xA157, 'project_x', type='subroutine',
           comment='3D X coordinate to screen column — perspective division')
proj.label(0xAD6C, 'load_camera', type='subroutine',
           comment='Copy camera pos/orientation to transform workspace')

# ── Rendering pipeline ──
proj.label(0xAF32, 'fill_column_fast', type='subroutine',
           comment='Fast vertical column fill — unrolled store sequence')
proj.label(0xB24D, 'rasterize_edge', type='subroutine',
           comment='Terrain edge draw — Bresenham-style line between projected vertices')
proj.label(0xB3A7, 'edge_midpoint', type='subroutine',
           comment='Terrain edge midpoint + fractal perturbation')
proj.label(0xB418, 'fill_terrain_column', type='subroutine',
           comment='Fill terrain column from edge to bottom with ground color')

# ── High-fan-in utilities (from call graph analysis) ──
proj.label(0x9290, 'queue_push', type='subroutine',
           comment='Command queue push — circular buffer at $071B, 32 slots')
proj.label(0x94AA, 'queue_push_flagged', type='subroutine',
           comment='Queue push with bit 7 set — flagged event variant')
proj.label(0x94B1, 'entity_load_state', type='subroutine',
           comment='Load entity state block from ROM tables into work area')
proj.label(0x92AA, 'entity_swap', type='subroutine',
           comment='Entity array management — swap/deactivate game objects')
proj.label(0x7CD7, 'memcpy_block', type='subroutine',
           comment='Block memory copy — src $B8, dest ($C2), len $C4/$C5')
proj.label(0x5AAE, 'pixel_shift_index', type='subroutine',
           comment='Compute pixel bit-shift index for ANTIC mode D rendering')
proj.label(0x5F75, 'random_select', type='subroutine',
           comment='Weighted random select — reads POKEY RANDOM, selects from table')
proj.label(0x476C, 'set_viewport_offset', type='subroutine',
           comment='Add offset to viewport pointers — configures draw region')
proj.label(0x4B39, 'clear_hud_line', type='subroutine',
           comment='Clear 14 bytes of HUD text buffer at $32B6')
proj.label(0xA754, 'plot_sprite_pixel', type='subroutine',
           comment='Plot masked sprite pixel to frame buffer with OR/AND blend')

# ── Display setup ──
proj.label(0x5997, 'build_screen_table', type='subroutine',
           comment='Build screen line address table — 45 ANTIC mode D rows, 96-byte stride')
proj.label(0x5DE4, 'patch_display_list', type='subroutine',
           comment='Update display list LMS addresses for scroll offset')
proj.label(0x7B0A, 'build_line_table', type='subroutine',
           comment='Build rendering line table for terrain perspective')
proj.label(0x7B42, 'setup_gameplay_display', type='subroutine',
           comment='Configure display list and screen memory for gameplay mode')
proj.label(0x7B4A, 'enable_display', type='subroutine',
           comment='Turn on DMA (SDMCTL) and enable DLI (NMIEN)')
proj.label(0x790A, 'set_pmg_positions', type='subroutine',
           comment='Write PMG horizontal positions and sizes from tables')
proj.label(0x83F8, 'update_pmg_data', type='subroutine',
           comment='Update player-missile shape data in PMG memory')

# ── Game logic ──
proj.label(0xA690, 'detect_pilot', type='subroutine',
           comment='Scan terrain surface for downed pilot — check pilot_map vs viewer')
proj.label(0x52E7, 'level_init', type='subroutine',
           comment='Level init — terrain seed, difficulty params, reset enemy/pilot state')
proj.label(0x7C80, 'game_init', type='subroutine',
           comment='Full game state init — variables, position, fuel, lives, shield')
proj.label(0x8530, 'read_joystick', type='subroutine',
           comment='Read joystick port and decode direction/fire')
proj.label(0x4956, 'setup_irq_vectors', type='subroutine',
           comment='Install custom IRQ/VBI vectors')
proj.label(0x6988, 'setup_game_vectors', type='subroutine',
           comment='Install gameplay VBI/DLI vectors')

# ── Sound ──
proj.label(0x90BD, 'sound_init', type='subroutine',
           comment='Sound system init — clear all POKEY channels')
proj.label(0x68BF, 'sound_engine_update', type='subroutine',
           comment='Sound engine tick — process sound effect queue')
proj.label(0x622F, 'sound_play_effect', type='subroutine',
           comment='Play sound effect — set frequency/control for effect ID')
proj.label(0x91E7, 'sound_set_channel', type='subroutine',
           comment='Set individual POKEY channel frequency and control')
proj.label(0x9510, 'sound_thrust', type='subroutine', comment='Thrust engine sound')
proj.label(0x9521, 'sound_explosion', type='subroutine', comment='Explosion sound effect')
proj.label(0x9551, 'sound_laser', type='subroutine', comment='Laser fire sound')
proj.label(0x9568, 'sound_warning', type='subroutine', comment='Warning alarm sound')
proj.label(0x9650, 'sound_pickup', type='subroutine', comment='Pilot pickup sound')
proj.label(0x9661, 'sound_landing', type='subroutine', comment='Landing approach sound')
proj.label(0x966E, 'sound_ambient', type='subroutine', comment='Ambient wind/environment')

# ── Utilities ──
proj.label(0x52C0, 'memzero', type='subroutine',
           comment='Zero-fill memory block — X=count, dest in $CB/$CC')
proj.label(0x7CAA, 'clear_screen', type='subroutine',
           comment='Fill screen memory $1070-$21B0 with zero')
proj.label(0x7C1C, 'mirror_charset', type='subroutine',
           comment='Mirror charset horizontally for HUD right-side text')
proj.label(0x7D9C, 'wait_n_frames', type='subroutine',
           comment='Wait N frames (count in A) synced to VBI counter $0609')
proj.label(0x7DAA, 'wait_60', type='subroutine', comment='Wait ~1 second (60 frames)')
proj.label(0x7DAF, 'wait_1', type='subroutine', comment='Wait 1 frame')
proj.label(0x7DB7, 'wait_2', type='subroutine', comment='Wait 2 frames')
proj.label(0x7DBC, 'wait_5', type='subroutine', comment='Wait 5 frames')
proj.label(0x7DC1, 'wait_10', type='subroutine', comment='Wait 10 frames')
proj.label(0x7DC6, 'wait_20', type='subroutine', comment='Wait 20 frames')

# ── Data regions ──
proj.mark_data(0x3800, 0x3BFF, hint='charset', label='charset',
               comment='Custom charset — 128 chars for cockpit/HUD')
proj.label(0x0900, 'terrain_height_buf', type='data',
           comment='256-byte fractal terrain height ring buffer')
proj.label(0x0A00, 'pilot_map', type='data',
           comment='256-byte pilot location tracking buffer')
proj.label(0x24BE, 'terrain_stack', type='data',
           comment='Fractal midpoint displacement recursion stack')
proj.label(0x4DD6, 'dli_jump_table', type='data',
           comment='DLI stage handler address table (8 entries, lo/hi pairs)')
proj.label(0x61EF, 'dli_table_alt', type='data',
           comment='Alternate DLI dispatch tables (2x8 entries)')
proj.label(0xB757, 'edge_adjacency', type='data',
           comment='Terrain edge neighbor lookup table')
proj.mark_data(0x6B9E, 0x7909, hint='bytes', label='terrain_lut',
               comment='Terrain rendering LUTs — perspective transform, height-to-screen')

# Save, export, verify
proj.save()

export_dir = os.path.join(PROJ_DIR, 'exports')
print(proj.export_asm(export_dir))
print(proj.verify_asm(export_dir))

# Stats
total = sum(e - s + 1 for s, e, _, _ in proj._segments if e - s > 0)
code = sum(int(r['end'], 16) - int(r['start'], 16) + 1 for r in proj._regions if r['type'] == 'code')
data = sum(int(r['end'], 16) - int(r['start'], 16) + 1 for r in proj._regions if r['type'] == 'data')
named = sum(1 for l in proj._labels.values() if not l['name'].startswith(('sub_', 'L_', 'loc_')))
print(f"\nCoverage: {code+data}/{total} ({(code+data)*100/total:.1f}%)")
print(f"Code: {code}, Data: {data}")
print(f"Labels: {len(proj._labels)} ({named} named), Comments: {len(proj._comments)}")
print(f"Procedures: {len(proj._procedures)}")
