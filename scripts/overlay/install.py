#!/usr/bin/env python3
"""Install the Linux/Proton external renderer as a per-user service."""
import argparse
import importlib.util
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
from capture import policy_configured

SERVICE='sixbox-local-render.service'
DEST=Path.home()/'.local/share/sixbox/overlay'
UNIT=Path.home()/'.config/systemd/user'/SERVICE

def run(*args):
    return subprocess.run(['systemctl','--user',*args],check=True,capture_output=True,text=True)

def quote(value):
    return '"'+str(value).replace('\\','\\\\').replace('"','\\"').replace('%','%%')+'"'

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--prefix',type=Path,default=Path.home()/'.local/share/Steam/steamapps/compatdata/440/pfx')
    parser.add_argument('--uninstall',action='store_true')
    parser.add_argument('--check',action='store_true')
    parser.add_argument('--update-only',action='store_true')
    parser.add_argument('--quiet',action='store_true')
    args=parser.parse_args()
    if sys.platform!='linux':raise SystemExit('Overlay render is Linux/Proton only.')
    if args.update_only and not UNIT.exists():return
    if not shutil.which('systemctl'):raise SystemExit('systemd user services unavailable; run render.py --directory <Proton LOCALAPPDATA/SixBox/local-render> manually.')
    if args.uninstall:
        if UNIT.exists():run('disable','--now',SERVICE);UNIT.unlink();run('daemon-reload')
        if not args.quiet:print('Overlay autostart removed; source and KWin rules preserved.')
        return
    if importlib.util.find_spec('PySide6') is None:raise SystemExit('Install scripts/overlay/requirements.txt with the Python used to run this installer.')
    if not policy_configured():raise SystemExit('Import scripts/overlay/cloakd-all-private.kwinrule in KDE Window Rules first. Capture exclusion must be supported by your KWin session.')
    if not os.environ.get('DISPLAY'):raise SystemExit('XWayland DISPLAY is unavailable.')
    if args.check:
        print('PySide6, XWayland and the configured KWin capture-exclusion rule are available. Verify your actual recording backend separately.')
        return
    protocol=args.prefix.expanduser().resolve()/'drive_c/users/steamuser/AppData/Local/SixBox/local-render'
    protocol.mkdir(parents=True,exist_ok=True,mode=0o700)
    DEST.parent.mkdir(parents=True,exist_ok=True)
    stage=Path(tempfile.mkdtemp(prefix='.overlay-',dir=DEST.parent))
    try:
        shutil.copytree(Path(__file__).resolve().parent,stage,dirs_exist_ok=True,
            ignore=shutil.ignore_patterns('__pycache__','*.pyc','*.so','*.so.*','build','*.egg-info'))
        stage.chmod(0o700)
        if UNIT.exists():run('stop',SERVICE)
        backup=DEST.with_name('overlay.previous')
        if backup.exists():shutil.rmtree(backup)
        if DEST.exists():os.replace(DEST,backup)
        os.replace(stage,DEST)
        UNIT.parent.mkdir(parents=True,exist_ok=True)
        text='\n'.join(['[Unit]','Description=SixBox external ESP renderer (Linux/Proton)','After=graphical-session.target',
            'PartOf=graphical-session.target','','[Service]','Type=simple','UMask=0077',
            'Environment=QT_QPA_PLATFORM=xcb','Environment='+quote('DISPLAY='+os.environ['DISPLAY']),
            'ExecStart='+quote(sys.executable)+' '+quote(DEST/'render.py')+' --directory '+quote(protocol),
            'Restart=on-failure','RestartSec=3','TimeoutStopSec=5','NoNewPrivileges=yes','',
            '[Install]','WantedBy=graphical-session.target',''])
        temp=UNIT.with_suffix('.tmp');temp.write_text(text);temp.chmod(0o600);os.replace(temp,UNIT)
        run('daemon-reload');run('enable',SERVICE);run('restart',SERVICE)
        if not args.quiet:print('Overlay service installed. It remains hidden until enabled ESP data arrives.')
    finally:
        if stage.exists():shutil.rmtree(stage)

if __name__=='__main__':main()
