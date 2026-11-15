#!/usr/bin/env python3
"""Install/update the SixBox Linux/Proton music bridge without root or a network port."""
import argparse
import importlib.util
import os
from pathlib import Path
import shutil
import subprocess
import sys


def quote(value):
    return '"' + str(value).replace('\\', '\\\\').replace('"', '\\"').replace('%', '%%') + '"'


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--prefix',type=Path,default=Path.home()/'.local/share/Steam/steamapps/compatdata/440/pfx')
    parser.add_argument('--check',action='store_true')
    parser.add_argument('--update-only',action='store_true')
    parser.add_argument('--no-start',action='store_true')
    parser.add_argument('--uninstall',action='store_true')
    parser.add_argument('--quiet',action='store_true')
    args=parser.parse_args()
    if sys.platform!='linux':
        raise SystemExit('This installer targets Linux/Proton and MPRIS.')
    destination=Path.home()/'.local/share/sixbox/karaoke'
    unit=Path.home()/'.config/systemd/user/sixbox-karaoke.service'
    protocol=args.prefix.expanduser().resolve()/'drive_c/users/steamuser/AppData/Local/SixBox/Karaoke'
    if args.uninstall:
        subprocess.run(['systemctl','--user','disable','--now','sixbox-karaoke.service'],check=False,capture_output=True)
        unit.unlink(missing_ok=True)
        subprocess.run(['systemctl','--user','daemon-reload'],check=True)
        print('Music bridge removed. User profiles and API keys were preserved.')
        return
    if args.update_only and not unit.exists():
        return
    if importlib.util.find_spec('dbus') is None:
        raise SystemExit('Install the system Python D-Bus binding (Fedora: sudo dnf install python3-dbus).')
    if not shutil.which('systemctl'):
        raise SystemExit('systemd user services are unavailable. Run bridge.py --directory <Proton LOCALAPPDATA/SixBox/Karaoke> manually.')
    if args.check:
        print('Python D-Bus: available; protocol directory:',protocol)
        return
    protocol.mkdir(parents=True,exist_ok=True,mode=0o700)
    os.chmod(protocol,0o700)
    destination.mkdir(parents=True,exist_ok=True,mode=0o700)
    for name in ('bridge.py', 'audio.py'):
        shutil.copy2(Path(__file__).with_name(name),destination/name)
        os.chmod(destination/name,0o600)
    unit.parent.mkdir(parents=True,exist_ok=True)
    text='\n'.join([
        '[Unit]','Description=SixBox local music and karaoke bridge','After=graphical-session.target','',
        '[Service]','Type=simple','UMask=0077',
        'ExecStart='+quote(sys.executable)+' '+quote(destination/'bridge.py')+' --directory '+quote(protocol),
        'Restart=on-failure','RestartSec=3','TimeoutStopSec=45','NoNewPrivileges=yes','',
        '[Install]','WantedBy=default.target',''])
    unit.write_text(text)
    subprocess.run(['systemctl','--user','daemon-reload'],check=True)
    if args.update_only:
        subprocess.run(['systemctl','--user','try-restart','sixbox-karaoke.service'],check=True)
    elif not args.no_start:
        subprocess.run(['systemctl','--user','enable','sixbox-karaoke.service'],check=True,capture_output=True)
        subprocess.run(['systemctl','--user','restart','sixbox-karaoke.service'],check=True)
    if not args.quiet:
        print('Installed sixbox-karaoke.service. API access and music controls stay idle until Karaoke is enabled.')
        print('Protocol directory:',protocol)

if __name__=='__main__':main()
