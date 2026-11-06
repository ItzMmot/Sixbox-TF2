#!/usr/bin/env python3
"""Build and install Sixbox FreeType AVX2 using the prepared Linux/MSVC toolchain."""
import argparse
import concurrent.futures
import fcntl
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import sys
import tempfile
from datetime import datetime, timezone
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[2]
SOURCE_SUFFIXES = {'.h', '.hpp', '.cpp', '.c', '.inl', '.inc', '.def', '.hlsl'}


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def win(path):
    return 'Z:' + str(path.resolve()).replace('/', '\\')


def version_key(path):
    return tuple(int(n) for n in re.findall(r'\d+', path.name))


def pick_directory(override, candidates, label):
    paths = [Path(override).expanduser()] if override else candidates
    for path in paths:
        if path.is_dir():
            return path.resolve()
    raise RuntimeError(f'{label} not found. See docs/building.md.')


def snapshot(root, folders, extras, config):
    """Content-based invalidation includes additions/deletions and header edits."""
    files = set(extras)
    for folder in folders:
        files.update(p for p in folder.rglob('*') if p.is_file() and p.suffix.lower() in SOURCE_SUFFIXES)
    inputs = {os.path.relpath(p, root): digest(p) for p in sorted(files)}
    payload = {'inputs': inputs, 'config': config}
    fingerprint = hashlib.sha256(json.dumps(payload, sort_keys=True).encode()).hexdigest()
    return fingerprint, payload


def project_sources(project):
    files = []
    for node in ET.parse(project).findall('.//{*}ClCompile'):
        name = node.get('Include')
        if not name:
            continue
        condition = node.get('Condition')
        if condition:
            exists = re.fullmatch(r"Exists\('([^']+)'\)", condition.strip())
            require(exists, f'Unsupported source condition: {condition}')
            if not (project.parent / exists.group(1).replace('\\', '/')).exists():
                continue
        # FreeType.props is handled explicitly below, with the matching backend.
        require('$(' not in name, f'Unsupported project source variable: {name}')
        path = (project.parent / name.replace('\\', '/')).resolve()
        require(path.is_file(), f'Project source missing: {path}')
        files.append(path)
    require(files, 'No source files found in the project')
    return list(dict.fromkeys(files))


def pe_imports(data):
    """Validate an AMD64 release DLL before touching the installed file."""
    require(len(data) >= 256 and data[:2] == b'MZ', 'Invalid DOS header')
    pe = struct.unpack_from('<I', data, 0x3c)[0]
    require(pe + 24 <= len(data) and data[pe:pe + 4] == b'PE\0\0', 'Invalid PE header')
    machine, sections, _, _, _, opt_size, flags = struct.unpack_from('<HHIIIHH', data, pe + 4)
    require(machine == 0x8664 and flags & 0x2000, 'Expected an AMD64 DLL')
    opt = pe + 24
    require(opt_size >= 128 and opt + opt_size + 40 * sections <= len(data), 'Truncated PE data')
    require(struct.unpack_from('<H', data, opt)[0] == 0x20b, 'Expected PE32+')
    entry = struct.unpack_from('<I', data, opt + 16)[0]
    image_size = struct.unpack_from('<I', data, opt + 56)[0]
    require(0 < entry < image_size, 'Invalid DLL entry point')
    mapping = []
    for i in range(sections):
        size, rva, rawsize, raw = struct.unpack_from('<IIII', data, opt + opt_size + 40 * i + 8)
        mapping.append((rva, rawsize, raw))

    def offset(rva, length=1):
        for start, size, raw in mapping:
            if start <= rva and rva + length <= start + size:
                position = raw + rva - start
                require(position + length <= len(data), 'Truncated PE section')
                return position
        raise RuntimeError(f'Unmapped PE RVA {rva:x}')

    import_rva = struct.unpack_from('<I', data, opt + 120)[0]
    require(import_rva != 0, 'Missing DLL import table')
    imports = []
    for index in range(1024):
        cursor = offset(import_rva + index * 20, 20)
        if not any(data[cursor:cursor + 20]):
            break
        name_rva = struct.unpack_from('<I', data, cursor + 12)[0]
        begin = offset(name_rva)
        end = data.find(b'\0', begin, min(len(data), begin + 512))
        require(end >= begin, 'Invalid DLL import name')
        name = data[begin:end].decode('ascii').lower()
        require(name not in {'msvcp140d.dll', 'vcruntime140d.dll', 'ucrtbased.dll',
                             'freetype.dll', 'freetype6.dll'}, f'Unexpected runtime dependency: {name}')
        imports.append(name)
    else:
        raise RuntimeError('Unterminated DLL import table')
    return imports


def install(source, target, backups):
    data = source.read_bytes()
    imports = pe_imports(data)
    sha = hashlib.sha256(data).hexdigest()
    require(target.parent.is_dir(), f'Proton drive not found: {target.parent}')
    backup = None
    if target.exists():
        old_sha = digest(target)
        if old_sha == sha:
            print(f'Already installed: {target}', flush=True)
            return {'sha256': sha, 'bytes': len(data), 'imports': imports, 'target': str(target), 'backup': None}
        backups.mkdir(parents=True, exist_ok=True)
        stamp = datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S%fZ')
        backup = backups / f'Amalgam-before-{stamp}-{old_sha[:12]}.dll'
        shutil.copy2(target, backup)
        require(digest(backup) == old_sha, 'Backup checksum mismatch')
    fd, temporary = tempfile.mkstemp(prefix='.sixbox-', suffix='.dll', dir=target.parent)
    try:
        with os.fdopen(fd, 'wb') as file:
            file.write(data)
            file.flush()
            os.fsync(file.fileno())
        os.chmod(temporary, 0o644)
        os.replace(temporary, target)
    finally:
        if os.path.exists(temporary):
            os.unlink(temporary)
    require(digest(target) == sha, 'Installed DLL checksum mismatch')
    print(f'Installed: {target}\nBackup: {backup}\nSHA256: {sha}', flush=True)
    return {'sha256': sha, 'bytes': len(data), 'imports': imports, 'target': str(target),
            'backup': str(backup) if backup else None}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--check', action='store_true', help='check dependencies without compiling')
    parser.add_argument('--clean', action='store_true', help='ignore the successful build cache')
    parser.add_argument('--no-install', action='store_true', help='build without replacing the Proton DLL')
    parser.add_argument('--jobs', type=int, default=2, help='parallel client compilations (default: 2)')
    args = parser.parse_args()
    require(args.jobs > 0, '--jobs must be positive')
    require(shutil.which('wine'), 'wine is required for the MSVC build')
    vc_base = ROOT / '.builds/tools/msvc/VC/Tools/MSVC'
    vc = pick_directory(os.getenv('SIXBOX_MSVC_TOOLS'), sorted(vc_base.glob('*'), key=version_key, reverse=True), 'MSVC')
    sdk = pick_directory(os.getenv('SIXBOX_WINDOWS_SDK'), [ROOT / '.builds/tools/msvc/Windows Kits/10'], 'Windows SDK')
    sdk_versions = sorted((sdk / 'Include').glob('*'), key=version_key, reverse=True)
    require(sdk_versions, 'Windows SDK includes not found')
    sdk_version = sdk_versions[0].name
    ft = pick_directory(os.getenv('SIXBOX_FREETYPE_SOURCE'), [ROOT / '.builds/deps/freetype',
        ROOT / '.builds/atlas-final-deps/freetype-VER-2-14-3', ROOT / '.builds/release/freetype'], 'FreeType source')
    backend = pick_directory(os.getenv('SIXBOX_IMGUI_FREETYPE'), [ROOT / '.builds/deps/imgui/misc/freetype',
        ROOT / '.builds/atlas-final-deps/imgui', ROOT / '.builds/release/imgui/misc/freetype'], 'ImGui FreeType backend')
    boost_source = Path(os.getenv('SIXBOX_BOOST_INCLUDE', '/usr/include'))
    required = [vc / 'bin/Hostx64/x64' / name for name in ['cl.exe', 'lib.exe', 'link.exe']]
    required += [vc / 'lib/x64/libcmt.lib',
                 ft / 'CMakeLists.txt', ft / 'include/ft2build.h', backend / 'imgui_freetype.cpp',
                 backend / 'imgui_freetype.h', boost_source / 'boost/version.hpp']
    require(any(p.name.casefold() == 'kernel32.lib' for p in (sdk / f'Lib/{sdk_version}/um/x64').glob('*')), 'Windows SDK kernel32 library missing')
    require(all(p.is_file() for p in required), 'Missing build dependencies: ' + ', '.join(str(p) for p in required if not p.is_file()))
    project = ROOT / 'Unibox/Unibox.vcxproj'
    sources = project_sources(project) + [backend / 'imgui_freetype.cpp']
    ft_names = re.search(r'set\(BASE_SRCS(.*?)\n\)', (ft / 'CMakeLists.txt').read_text(), re.S)
    require(ft_names, 'FreeType source list not found in CMakeLists.txt')
    ft_sources = [ft / name for name in ft_names.group(1).split()] + [ft / 'builds/windows/ftsystem.c', ft / 'builds/windows/ftdebug.c']
    require(all(p.is_file() for p in ft_sources), 'A FreeType source file is missing')
    if args.check:
        print(f'PASS: MSVC {vc.name}, SDK {sdk_version}, FreeType, ImGui backend, Boost; {len(sources)} client units. No DLL build.')
        return

    out = ROOT / '.builds/local-dll'
    out.mkdir(parents=True, exist_ok=True)
    with (out / 'build.lock').open('w') as lock:
        try:
            fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError:
            raise RuntimeError('Another ./build.sh build is running') from None
        build(args, vc, sdk, sdk_version, ft, backend, boost_source, sources, ft_sources, out, project)


def build(args, vc, sdk, sdk_version, ft, backend, boost_source, sources, ft_sources, out, project):
    shim = out / 'headers'
    shim.mkdir(exist_ok=True)
    boost_link = shim / 'boost'
    if boost_link.is_symlink() and boost_link.resolve() != (boost_source / 'boost').resolve():
        boost_link.unlink()
    if not boost_link.exists():
        boost_link.symlink_to((boost_source / 'boost').resolve(), target_is_directory=True)
    env = dict(os.environ, WINEPREFIX=str(ROOT / '.builds/tools/wine'), WINEDEBUG='-all',
               WINEDLLOVERRIDES='mscoree,mshtml=', DISPLAY='', WINEPATH=win(vc / 'bin/Hostx64/x64'))
    env['INCLUDE'] = ';'.join(map(win, [vc / 'include', *[sdk / f'Include/{sdk_version}/{name}'
        for name in ['ucrt', 'shared', 'um', 'winrt']], shim, ROOT / 'Unibox/include']))
    env['LIB'] = ';'.join(map(win, [vc / 'lib/onecore/x64', vc / 'lib/x64',
        sdk / f'Lib/{sdk_version}/ucrt/x64', sdk / f'Lib/{sdk_version}/um/x64']))
    flags = ['/nologo', '/c', '/std:c++latest', '/permissive-', '/EHsc', '/MD', '/O2', '/bigobj',
             '/DNDEBUG', '/D_CONSOLE', '/D_DISABLE_CONSTEXPR_MUTEX_CONSTRUCTOR',
             '/D__CONFIGURATION__="ReleaseAVX2"', '/D__PLATFORM__="x64"', '/W0', '/utf-8',
             '/arch:AVX2', '/DIMGUI_ENABLE_FREETYPE', '/I' + win(ft / 'include'),
             '/I' + win(backend), '/I' + win(ROOT / 'Unibox/include/ImGui')]
    folders = [ROOT / 'Unibox/src', ROOT / 'Unibox/include', ft, backend]
    extras = [Path(__file__).resolve(), ROOT / 'build.sh', project, ft / 'CMakeLists.txt', *sources,
              *ROOT.joinpath('Unibox').glob('*.props'), ROOT / '.github/scripts/build-aura-shaders.py']
    config = {'flags': flags, 'vc': str(vc), 'sdk': str(sdk), 'sdk_version': sdk_version,
              'cl_sha256': digest(vc / 'bin/Hostx64/x64/cl.exe'), 'boost': str(boost_source.resolve())}
    fingerprint, inputs = snapshot(ROOT, folders, extras, config)
    state_file = out / 'success.json'
    try:
        state = json.loads(state_file.read_text())
    except (FileNotFoundError, ValueError):
        state = {}
    output = ROOT / 'output/x64/ReleaseAVX2/Sixboxx64ReleaseFreetypeAVX2.dll'
    reusable = not args.clean and state.get('fingerprint') == fingerprint and output.is_file() and state.get('sha256') == digest(output)
    subprocess.run([sys.executable, str(ROOT / '.github/scripts/build-aura-shaders.py'), '--check'], cwd=ROOT, check=True)
    if reusable:
        pe_imports(output.read_bytes())
        print('Inputs unchanged; reusing the verified DLL.', flush=True)
    else:
        print('Inputs changed or cache missing; rebuilding all client objects (FreeType AVX2).', flush=True)
        objects, logs = out / 'objects', out / 'logs'
        for directory in [objects, logs]:
            if directory.exists():
                shutil.rmtree(directory)
            directory.mkdir()

        def invoke(tool, command, log):
            with log.open('w') as file:
                result = subprocess.run(['wine', win(vc / 'bin/Hostx64/x64' / tool), *command],
                                        cwd=ROOT, env=env, stdout=file, stderr=subprocess.STDOUT)
            if result.returncode:
                raise RuntimeError(f'{tool} failed ({result.returncode}): {log}\n' + log.read_text(errors='replace')[-6000:])

        header = win(ROOT / 'Unibox/src/SDK/SDK.h')
        pch = ['/FI' + header, '/Fp' + win(objects / 'sdk.pch')]
        pch_source = objects / 'pch.cpp'
        pch_source.write_text('// SDK precompiled header\n')
        invoke('cl.exe', [*flags, *pch, '/Yc' + header, '/Fo' + win(objects / 'pch.obj'), win(pch_source)], logs / 'pch.log')
        ft_objects = []
        for index, source in enumerate(ft_sources):
            obj = objects / f'freetype_{index}.obj'
            invoke('cl.exe', ['/nologo', '/c', '/MD', '/O2', '/W0', '/DFT2_BUILD_LIBRARY',
                '/I' + win(ft / 'include'), '/Fo' + win(obj), win(source)], logs / f'freetype_{index}.log')
            ft_objects.append(obj)
        library = objects / 'freetype.lib'
        invoke('lib.exe', ['/NOLOGO', '/OUT:' + win(library), *map(win, ft_objects)], logs / 'freetype-lib.log')
        print('PCH and static FreeType ready.', flush=True)
        units = []
        for index, source in enumerate(sources):
            obj = objects / f'client_{index}.obj'
            units.append((index, source, obj))

        def compile_unit(unit):
            index, source, obj = unit
            command = [*flags, '/Fo' + win(obj), win(source)]
            use_pch = source.suffix == '.cpp' and source != backend / 'imgui_freetype.cpp' and source.name not in {'bitbuf.cpp', 'NetVars.cpp'} and '/imgui/' not in str(source).lower()
            if use_pch:
                command += [*pch, '/Yu' + header]
            invoke('cl.exe', command, logs / f'{index}_{source.stem}.log')

        with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:
            for completed, _ in enumerate(pool.map(compile_unit, units), 1):
                if completed % 20 == 0 or completed == len(units):
                    print(f'Compiled {completed}/{len(units)}', flush=True)
        output.parent.mkdir(parents=True, exist_ok=True)
        libraries = ['kernel32', 'user32', 'gdi32', 'winspool', 'comdlg32', 'advapi32', 'shell32',
                     'ole32', 'oleaut32', 'uuid', 'odbc32', 'odbccp32', 'd3d9', 'winmm', 'ws2_32', 'psapi', 'shlwapi']
        command = ['/NOLOGO', '/DLL', '/MACHINE:X64', '/INCREMENTAL:NO', '/OPT:REF', '/OPT:ICF',
                   '/SUBSYSTEM:CONSOLE', '/OUT:' + win(output), win(objects / 'pch.obj'),
                   *[win(obj) for _, _, obj in units], win(library), *[name + '.lib' for name in libraries]]
        response = out / 'link.rsp'
        response.write_text('\n'.join('"' + arg + '"' for arg in command))
        invoke('link.exe', ['@' + win(response)], logs / 'link.log')
        pe_imports(output.read_bytes())
        require(snapshot(ROOT, folders, extras, config)[0] == fingerprint, 'Source changed during build; installation cancelled. Run ./build.sh again.')
        require(all(obj.is_file() and obj.stat().st_size for _, _, obj in units), 'A client object is missing')
        state = {'fingerprint': fingerprint, 'sha256': digest(output), 'bytes': output.stat().st_size,
                 'objects': len(units), 'configuration': 'ReleaseAVX2 + static FreeType', 'inputs': inputs}
        state_file.write_text(json.dumps(state, indent=2))
        print(f'Linked: {output}', flush=True)
    if not args.no_install:
        drive = Path(os.getenv('SIXBOX_PROTON_DRIVE', str(Path.home() / '.local/share/Steam/steamapps/compatdata/440/pfx/drive_c'))).expanduser()
        receipt = install(output, drive / 'Amalgamx64ReleaseFreetypeAVX2.dll', out / 'backups')
        receipt['fingerprint'] = fingerprint
        (out / 'installed.json').write_text(json.dumps(receipt, indent=2))
    print(f'Done. DLL: {output}\nSHA256: {digest(output)}', flush=True)


if __name__ == '__main__':
    try:
        main()
    except (RuntimeError, OSError, subprocess.SubprocessError, ET.ParseError, struct.error) as error:
        print(f'Build failed: {error}', file=sys.stderr)
        sys.exit(1)
