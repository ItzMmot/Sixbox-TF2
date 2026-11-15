#!/usr/bin/env python3
"""Package source-only Linux helpers; never include workspace captures or native build outputs."""
import argparse
from pathlib import Path
import zipfile
ROOT=Path(__file__).resolve().parents[2]
def files(root=ROOT):
    for folder in ['scripts/overlay','scripts/karaoke']:
        for path in sorted((root/folder).rglob('*')):
            if not path.is_file():continue
            parts=path.relative_to(root).parts
            if any(part in {'__pycache__','build','.pytest_cache'} or part.endswith('.egg-info') for part in parts):continue
            if path.suffix in {'.pyc','.so','.dll','.exe','.pdb'} or '.so.' in path.name:continue
            yield path
    for name in ['docs/overlay.md','docs/karaoke.md','LICENSE.md']:yield root/name

def package(destination,root=ROOT):
    destination=Path(destination);destination.parent.mkdir(parents=True,exist_ok=True)
    temporary=destination.with_suffix(destination.suffix+'.tmp')
    try:
        with zipfile.ZipFile(temporary,'w',zipfile.ZIP_DEFLATED) as archive:
            for path in files(root):
                info=zipfile.ZipInfo(path.relative_to(root).as_posix(),(2026,1,1,0,0,0))
                info.compress_type=zipfile.ZIP_DEFLATED;info.external_attr=0o100644<<16
                archive.writestr(info,path.read_bytes())
        temporary.replace(destination)
    finally:temporary.unlink(missing_ok=True)
    return destination
if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--output',type=Path,required=True)
    print(package(parser.parse_args().output))
