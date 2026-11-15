# Cloakd Render SDK

Vendored from the supplied Cloakd renderer SDK 0.3.0, under the included MIT license.
The SixBox overlay uses its Python/Qt Quick custom-item path and does not require
the optional native graph plugin. No prebuilt ELF libraries are distributed.

To build that optional SDK plugin and regenerate its shader assets, install
Qt 6.7+ Quick/QML development tools, Ninja, CMake and qsb, then run `bash build.sh`
in this directory. Native plugin outputs are ignored by Git.
