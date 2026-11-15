#!/usr/bin/env bash
set -euo pipefail
cd -- "$(dirname -- "${BASH_SOURCE[0]}")"
cmake -S native -B build/native -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/native --parallel "${CLOAKD_JOBS:-4}"
mkdir -p cloakd_render/qml/Cloakd/Native
cp build/native/qml/Cloakd/Native/libcloakdnative*.so cloakd_render/qml/Cloakd/Native/
cp build/native/qml/Cloakd/Native/qmldir build/native/qml/Cloakd/Native/cloakdnative.qmltypes cloakd_render/qml/Cloakd/Native/
python3 - <<'PY'
from pathlib import Path
from cloakd_render.shaders import compile_shader
for path in Path('cloakd_render/shaders').glob('*.frag'):
    compile_shader(path, path.with_suffix('.qsb'))
PY
