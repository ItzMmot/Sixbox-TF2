# Overlay render (Linux only)

ESP → **Overlay render (Linux only)** routes selected boxes, nametags and model
stencil outlines to an external Cloakd window. Box glow and box stencil have
separate settings; model stencil follows the actual model silhouette.

The DLL supports Windows x64 and Linux/Proton. This external renderer requires
Linux, a KDE/KWin session supporting the `excludefromcapture` window rule,
XWayland, systemd user services and Python 3.10+. It is unavailable on native
Windows and compositors without that rule. Regular in-game ESP remains available.

## Install

Use a checkout or extract `SixBox-Linux-Helpers.zip` from Releases. From its root:

```sh
python3 -m venv ~/.local/share/sixbox/overlay-env
~/.local/share/sixbox/overlay-env/bin/pip install -r scripts/overlay/requirements.txt
```

Import `scripts/overlay/cloakd-all-private.kwinrule` in KDE System Settings →
Window Management → Window Rules. The installer never edits your existing rules.
Your KWin version must support capture exclusion; the checked rule is required
before the overlay will display anything. Install Qt's X11/EGL system dependencies
for your distribution if Qt reports a missing platform library
([Qt requirements](https://doc.qt.io/qt-6/linux-requirements.html)).

```sh
~/.local/share/sixbox/overlay-env/bin/python scripts/overlay/install.py --check
~/.local/share/sixbox/overlay-env/bin/python scripts/overlay/install.py
```

For a nondefault Steam library, append `--prefix /path/to/compatdata/440/pfx` to
installation. Restart TF2 with the current DLL and enable the required ESP groups
and overlay routes. `systemctl --user status sixbox-local-render.service` shows
service errors. `journalctl --user -u sixbox-local-render.service` shows logs.

Before relying on recording exclusion, test your actual OBS/Discord capture mode.
A configured KWin rule is checked, but cannot guarantee exclusion for every
recording backend. Missing policy, stale frames, lost focus or an absent helper
hide the selected overlay elements instead of drawing them inside the game.

## Updates and removal

Rerun the installer after updating the helper sources. `./build.sh` updates an
already installed service; it does not enable a service on a fresh machine.
`--uninstall` removes autostart and stops the service, retaining sources and rules.
User configs are not modified. Installed sources live in
`~/.local/share/sixbox/overlay`; the previous version is retained as
`overlay.previous`. The transport is local files beneath the Proton prefix's
`drive_c/users/steamuser/AppData/Local/SixBox/local-render`, without a network port.

All required SDK Python/QML and shader assets are vendored under
`scripts/overlay/vendor` with their MIT license. No precompiled ELF plugin is
required. Optional native graph plugin sources and build instructions are included
for SDK development; they are independent of the SixBox rendering path.

## Checks

```sh
python3 tests/overlay/test_overlay.py
python3 tests/overlay/test_model_stencil.py
python3 tests/overlay/test_distribution.py
```

These run Qt offscreen and cover routing, stale data, scaling, box effects and
model contours. They do not replace an in-game test or recording verification.
