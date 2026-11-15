import hashlib
import os
from pathlib import Path
import shutil
import subprocess
import tempfile


def compile_shader(source, destination=None):
    source = Path(source).resolve()
    if source.suffix != '.frag' or not source.is_file() or source.stat().st_size > 1048576:
        raise ValueError('Expected a .frag file up to 1 MiB')
    qsb = next((path for name in ('qsb-qt6', 'qsb', '/usr/lib64/qt6/bin/qsb') if (path := shutil.which(name))), None)
    if not qsb:
        raise RuntimeError('Qt qsb shader compiler is missing')
    digest = hashlib.sha256(source.read_bytes()).hexdigest()[:20]
    cache = Path(os.environ.get('XDG_CACHE_HOME', Path.home()/'.cache')) / 'cloakd-render'
    destination = Path(destination) if destination else cache / (digest + '.qsb')
    if destination.resolve()==source or destination.suffix!='.qsb':
        raise ValueError('Output must be a separate .qsb file')
    destination.parent.mkdir(parents=True, exist_ok=True)
    fd, temporary = tempfile.mkstemp(dir=destination.parent, suffix='.qsb')
    os.close(fd)
    try:
        result = subprocess.run([qsb, '--glsl', '100 es,120,150', '--hlsl', '50', '--msl', '12', '-o', temporary, str(source)], capture_output=True, text=True, timeout=20)
        if result.returncode:
            raise ValueError(result.stderr[-5000:] + result.stdout[-2000:])
        os.replace(temporary, destination)
    except subprocess.TimeoutExpired as exc:
        raise RuntimeError('Shader compilation timed out') from exc
    finally:
        Path(temporary).unlink(missing_ok=True)
    return str(destination.resolve())
