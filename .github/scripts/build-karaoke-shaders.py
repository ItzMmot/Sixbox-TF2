#!/usr/bin/env python3
"""Import validated DX9 shader blobs, or verify source/bytecode hashes on any OS."""
import argparse
import hashlib
from pathlib import Path
import re
import struct
ROOT=Path(__file__).resolve().parents[2]
SOURCE=ROOT/'SixBox/src/Features/Visuals/Karaoke/KaraokeGlass.hlsl'
OUTPUT=SOURCE.with_name('KaraokeGlassBytecode.h')

def validate(blob,vertex=False):
    if len(blob)<8 or len(blob)%4:
        raise ValueError('Invalid shader bytecode length')
    words=struct.unpack('<'+'I'*(len(blob)//4),blob)
    if words[0]!=(0xfffe0300 if vertex else 0xffff0300) or words[-1]!=0x0000ffff:
        raise ValueError('Expected a complete Shader Model 3 program')
    return words

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--check',action='store_true')
    parser.add_argument('--from-binaries',type=Path)
    args=parser.parse_args()
    digest=hashlib.sha256(SOURCE.read_bytes()).hexdigest()
    if args.check:
        text=OUTPUT.read_text()
        if f'SourceHash[]="{digest}"' not in text:
            raise RuntimeError('Karaoke HLSL changed; regenerate bytecode before building')
        for name in ('Blur','Glass','Vertex','Text'):
            values=re.search(rf'{name}\[\]=\{{(.*?)\}};',text,re.S).group(1)
            words=[int(v,16) for v in re.findall(r'0x([0-9a-fA-F]+)',values)]
            validate(struct.pack('<'+'I'*len(words),*words),name=='Vertex')
        print('Karaoke: HLSL digest and all four SM3 programs verified')
        return
    if not args.from_binaries:
        raise RuntimeError('Use --from-binaries after compiling blur/glass/text to ps_3_0 and vertex to vs_3_0')
    lines=['#pragma once','#include <cstdint>','namespace Karaoke::Shader {',f'inline constexpr char SourceHash[]="{digest}";']
    for name in ('Blur','Glass','Vertex','Text'):
        words=validate((args.from_binaries/(name.lower()+'.bin')).read_bytes(),name=='Vertex')
        lines.append('inline constexpr uint32_t '+name+'[]={')
        for offset in range(0,len(words),8):
            lines.append(','.join(f'0x{word:08x}' for word in words[offset:offset+8])+',')
        lines.append('};')
    lines.append('}')
    OUTPUT.write_text('\n'.join(lines)+'\n')
if __name__=='__main__':main()
