"""Use Cloakd's native KWin exclusion policy on a regular managed window."""
import configparser
import os
from pathlib import Path

APP_ID = 'io.github.cloakd.overlay'
TITLE = 'Cloakd · sixbox-local-esp'


def policy_configured(path=None):
    path = path or Path(os.environ.get('XDG_CONFIG_HOME', str(Path.home()/'.config'))) / 'kwinrulesrc'
    parser = configparser.ConfigParser(interpolation=None, strict=False)
    try:
        parser.read(path, encoding='utf-8')
        rules = parser.get('General', 'rules', fallback='').split(',')
        for name in rules:
            section = parser[name] if parser.has_section(name) else {}
            if (section.get('wmclass') == APP_ID and section.get('wmclassmatch') == '1'
                    and section.get('wmclasscomplete', 'false') == 'false'
                    and section.get('titlematch', '0') == '0'
                    and section.get('excludefromcapture') == 'true'
                    and section.get('excludefromcapturerule') == '2'
                    and section.get('layer') == 'overlay' and section.get('layerrule') == '2'):
                return True
    except (OSError, configparser.Error):
        return False
    return False
