"""
pcb_setup
Sets the board's layer count and net classes in the running KiCad, through
KiCad's own IPC API (kicad-python) - KiCad writes the files, never a text
editor. The values are those of PLAN.md section 0.6: JLCPCB's standard
4-layer stackup JLC04161H-7628 (1.6 mm), net classes for signals, USB,
motor coils and the power rails.

It is not the whole board setup. KiCad 10.0.5's API cannot yet change the
physical stackup (UpdateBoardStackup is "not yet implemented") nor the net
class patterns, and its SetNetClasses creates the classes but drops their
values (track, clearance, vias, diff pair). The script reads back what
landed and prints everything else for Board Setup by hand; once KiCad
takes the values, that part of the list disappears by itself.

Usage (KiCad running, Scribbler.kicad_pcb open in the PCB editor, the API
enabled under Preferences > Plugins):
    pip install kicad-python
    python scripts/pcb_setup.py [--dry-run]
then save the board in KiCad (Ctrl+S) - the script does not save.

Running it again is harmless: classes are merged by name, the layer count
is only raised.

@author   Franz Kugler / franz _AT_ franz _MINUS_ kugler _DOT_ de
@version  1.0
@created  18.11.2026
@updated  18.11.2026
"""
import argparse
import sys

try:
    from kipy import KiCad
    from kipy.project_types import NetClass
    from kipy.proto.board import board_types_pb2
    from kipy.proto.common.commands import project_commands_pb2
    from kipy.proto.common.types import MapMergeMode, project_settings_pb2
    from google.protobuf.empty_pb2 import Empty
except ImportError:
    sys.exit('kicad-python missing: pip install kicad-python')

BOARD = 'Scribbler.kicad_pcb'
COPPER_LAYERS = 4
MM = 1_000_000  # KiCad's API counts nanometres

# name: (track, clearance, via diameter, via drill[, diff-pair width, gap]), in mm.
# Vias stay within JLCPCB's standard (drill >= 0.3, ring >= 0.15). The USB
# pair is ~90 ohm on the 0.21 mm 7628 prepreg (Hammerstad / IPC-2141, with
# solder mask); the S3 runs full speed only, so no paid impedance control.
CLASSES = {
    'Default': (0.20, 0.20, 0.6, 0.3),
    # 0.30 / 0.20, not 0.25 / 0.15: a pair gap under the 0.20 clearance fails DRC.
    'USB': (0.30, 0.20, 0.6, 0.3, 0.30, 0.20),
    'Motor': (0.80, 0.25, 0.8, 0.4),
    # 0.25, not more: the TMC2240's VS pins sit 0.25 mm from its coil outputs.
    'Power_VS': (1.00, 0.25, 0.8, 0.4),
    'Power_5V': (1.00, 0.20, 0.8, 0.4),
    'Power_3V3': (0.50, 0.20, 0.6, 0.3),
    'GND': (0.50, 0.20, 0.6, 0.3),
}

# Not settable through KiCad's API - Konnect's assign_net_to_class writes
# them (PLAN.md 0.6); printed here as the reference. Schematic labels carry
# the sheet path, hence /MOT*.
PATTERNS = [
    ('USB_D*', 'USB'),
    ('*ESP_USB_D*', 'USB'),
    ('/MOT*', 'Motor'),
    ('VS', 'Power_VS'),
    ('Net-(U2-SW)', 'Power_VS'),
    ('+5V', 'Power_5V'),
    ('+5VD', 'Power_5V'),
    ('+3.3V', 'Power_3V3'),
    ('GND', 'GND'),
]

# Not settable through the API either - Board Setup > Board Stackup.
STACKUP = [
    ('F.Cu', 'signal', 'copper 1 oz', 0.035, None),
    ('dielectric 1', 'prepreg', '7628', 0.2104, 4.4),
    ('In1.Cu', 'power (GND)', 'copper 0.5 oz', 0.0152, None),
    ('dielectric 2', 'core', 'FR4', 1.065, 4.6),
    ('In2.Cu', 'power (VS / 5V / 3V3 islands)', 'copper 0.5 oz', 0.0152, None),
    ('dielectric 3', 'prepreg', '7628', 0.2104, 4.4),
    ('B.Cu', 'signal', 'copper 1 oz', 0.035, None),
]


def nm(mm):
    return round(mm * MM)


def build_classes(default):
    """Each class starts as a copy of KiCad's Default, so the via stack and the
    schematic settings are complete; only the board values are ours."""
    out = []
    for priority, (name, values) in enumerate(CLASSES.items()):
        proto = project_settings_pb2.NetClass()
        proto.CopyFrom(default)
        proto.name = name
        nc = NetClass(proto)
        # Default must stay the lowest priority (KiCad's own convention).
        nc.priority = default.priority if name == 'Default' else priority
        track, clearance, via_d, via_drill = values[:4]
        nc.track_width = nm(track)
        nc.clearance = nm(clearance)
        nc.via_diameter = nm(via_d)
        nc.via_drill = nm(via_drill)
        if len(values) > 4:
            nc.diff_pair_track_width = nm(values[4])
            nc.diff_pair_gap = nm(values[5])
            nc.diff_pair_via_gap = nm(values[5])
        out.append(nc)
    return out


def same(wanted, got):
    """True when KiCad holds the board values we sent."""
    if got is None:
        return False
    fields = ['track_width', 'clearance', 'via_diameter', 'via_drill']
    if wanted.name == 'USB':
        fields += ['diff_pair_track_width', 'diff_pair_gap']
    return all(getattr(wanted, f) == getattr(got, f) for f in fields)


def main():
    parser = argparse.ArgumentParser(description=__doc__.split('\n')[2])
    parser.add_argument('--dry-run', action='store_true', help='show, change nothing')
    args = parser.parse_args()

    kicad = KiCad()
    print(f'KiCad {kicad.get_version()}')
    board = kicad.get_board()
    if board.name != BOARD:
        sys.exit(f'open board is {board.name}, expected {BOARD}')

    copper = board.get_copper_layer_count()
    if copper < COPPER_LAYERS:
        print(f'copper layers: {copper} -> {COPPER_LAYERS}')
        if not args.dry_run:
            others = [
                layer for layer in board.get_enabled_layers()
                if not board_types_pb2.BoardLayer.Name(layer).endswith('_Cu')
            ]
            board.set_enabled_layers(COPPER_LAYERS, others)
    else:
        print(f'copper layers: {copper}, unchanged')

    project = board.get_project()
    current = {nc.name: nc for nc in project.get_net_classes()}
    classes = build_classes(current['Default']._proto)
    for nc in classes:
        dp = (f', pair {nc.diff_pair_track_width / MM:g}/{nc.diff_pair_gap / MM:g}'
              if nc.name == 'USB' else '')
        print(f'net class {nc.name:10} track {nc.track_width / MM:g}, clearance '
              f'{nc.clearance / MM:g}, via {nc.via_diameter / MM:g}/{nc.via_drill / MM:g}{dp}'
              f'{"" if nc.name in current else "  (new)"}')
    by_hand = classes
    if not args.dry_run:
        cmd = project_commands_pb2.SetNetClasses()
        cmd.net_classes.extend(nc._proto for nc in classes)
        cmd.merge_mode = MapMergeMode.MMM_MERGE
        project._kicad.send(cmd, Empty)
        # KiCad 10.0.5 takes the names and drops the board values of every
        # class sent (seen 2026-11-18), so read back what actually landed.
        landed = {nc.name: nc for nc in project.get_net_classes()}
        by_hand = [nc for nc in classes if not same(nc, landed.get(nc.name))]
        print(f'KiCad now has: {", ".join(sorted(landed))}')

    if by_hand:
        print('\nBy hand, Board Setup > Net Classes (values the API did not take):')
        print(f'  {"class":10} {"track":>6} {"clear.":>6} {"via":>5} {"drill":>5} {"DP w":>5} {"DP gap":>6}')
        for nc in by_hand:
            dp = (f'{nc.diff_pair_track_width / MM:5g} {nc.diff_pair_gap / MM:6g}'
                  if nc.name == 'USB' else '')
            print(f'  {nc.name:10} {nc.track_width / MM:6g} {nc.clearance / MM:6g} '
                  f'{nc.via_diameter / MM:5g} {nc.via_drill / MM:5g} {dp}')

    print('\nBy hand, Board Setup > Board Stackup > Physical Stackup (JLC04161H-7628):')
    for layer, kind, material, thickness, er in STACKUP:
        print(f'  {layer:13} {kind:32} {material:14} {thickness:g} mm'
              + (f', er {er:g}' if er else ''))
    print('  Board Editor Layers: In1.Cu and In2.Cu as type "power".')
    print('\nBy hand, Board Setup > Net Classes > Netclass assignments:')
    for pattern, name in PATTERNS:
        print(f'  {pattern:16} -> {name}')
    if not args.dry_run:
        print('\nThen save the board in KiCad.')


if __name__ == '__main__':
    main()
