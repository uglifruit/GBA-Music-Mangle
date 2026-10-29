#!/usr/bin/env python3
"""Cross-check fixture using an external, unmodified MIT shinen-gax-python serializer.
Usage: make_reference_fixture.py /path/to/shinen-gax-python output.gba
The notes/waveform below are authored test data, not a ripped soundtrack.
"""
import pathlib
import struct
import sys
sys.dont_write_bytecode = True
sys.path.insert(0, sys.argv[1])
from libs import shinen_gax as gax
from libs import gax_constructors as construct
from libs.gax_enums import step_effect

module = construct.gax_module_template(auth_name='GAX PoC synthetic fixture')
instrument = module.instrument_set[1]
instrument.header['wave_slots'] = [1, 0, 0, 0]
instrument.perf_list['perf_list_data'][0]['note'] = 3
instrument.perf_list['perf_list_data'][0]['wave_slot_id'] = 1
instrument.wave_params[0]['loop_end'] = 256
module.wave_set.wave_bank.append(bytes(16 + (i % 32) * 7 for i in range(256)))
song = module.song_bank['songs'][0]['songdata']
module.song_bank['songs'][0]['songname'] = 'Synthetic GAX proof'
song.properties.channel_count = 1
song.properties.step_count = 8
song.properties.song_length = 1
song.properties.restart_position = 0
song.patterns = [[gax.step_command() for _ in range(8)]]
for row, note in [(0, 49), (4, 53)]:
    command = song.patterns[0][row]
    command.semitone = note
    command.instrument = 1
song.patterns[0][0].effect_type = step_effect(15)
song.patterns[0][0].effect_param = 4
song.order_list = [[[0, 0]]]
blob = gax.pack_GAX_file(module, blob_offset=0x08000000)
pathlib.Path(sys.argv[2]).write_bytes(blob)
print('Song bus address:', hex(0x08000000 + struct.unpack_from('<I', blob, 4)[0]))
