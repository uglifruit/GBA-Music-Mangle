#!/usr/bin/env python3
"""Check SMaL operand byte lengths against an external, unmodified BinarySerializer snapshot.
Usage: check_reference.py /path/to/BinarySerializer.Audio.GBA
"""
import pathlib
import re
import sys

source=(pathlib.Path(sys.argv[1])/'src/MusyX/SoundMacro/MusyX_SMaL.cs').read_text(encoding='utf-8-sig')
enum=source[source.index('public enum CommandType'):source.index('#region Data')]
lengths=[]
for name,number in re.findall(r'^\s*(\w+)\s*=\s*(\d+),',enum,re.M):
    if int(number) in (0,1):
        lengths.append(1)
        continue
    match=re.search(r'public class '+name+r' : CommandData\s*\{(.*?)(?=\n        public class|\n        #endregion)',source,re.S)
    widths={'bool':1,'byte':1,'sbyte':1,'short':2,'ushort':2,'int':4,'uint':4}
    lengths.append(1+sum(widths[t] for t in re.findall(r's\.Serialize<(\w+)>',match[1])))
header=(pathlib.Path(__file__).resolve().parents[1]/'macro_lengths.h').read_text()
ours=list(map(int,re.findall(r'\d+',header[header.index('{'):])))
assert len(lengths)==54 and ours==lengths,(ours,lengths)
print('PASS all 54 SMaL byte lengths match the external parser')
