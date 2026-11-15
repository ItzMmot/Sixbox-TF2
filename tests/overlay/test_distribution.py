"""Distribution must work without private files, compiled plugins or user captures."""
import importlib.util
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
import zipfile

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'scripts/overlay'))

def module(name,path):
    spec=importlib.util.spec_from_file_location(name,path)
    result=importlib.util.module_from_spec(spec);spec.loader.exec_module(result)
    return result

class DistributionTest(unittest.TestCase):
    def test_source_bundle_is_self_contained(self):
        package=module('helper_package',ROOT/'.github/scripts/package-linux-helpers.py')
        with tempfile.TemporaryDirectory() as tmp:
            path=package.package(Path(tmp)/'helpers.zip')
            with zipfile.ZipFile(path) as archive:
                names=set(archive.namelist())
                for required in ['docs/overlay.md','docs/karaoke.md','scripts/overlay/vendor/LICENSE',
                    'scripts/overlay/vendor/cloakd_render/qml/RenderWindow.qml',
                    'scripts/overlay/vendor/native/CMakeLists.txt','scripts/overlay/cloakd-all-private.kwinrule',
                    'scripts/karaoke/install.py','scripts/overlay/install.py']:
                    self.assertIn(required,names)
                self.assertFalse(any('.private/' in n or '__pycache__' in n or '.so' in n or n.endswith(('.exe','.dll','.png','.json')) for n in names))
                archive.extractall(Path(tmp)/'extracted')
            result=subprocess.run([sys.executable,'-c',"import sys; sys.path.insert(0,'scripts/overlay'); import render, protocol, capture, model_image"],cwd=Path(tmp)/'extracted',capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stderr)

    def test_update_only_without_service_is_noop(self):
        from unittest.mock import patch
        installer=module('overlay_installer',ROOT/'scripts/overlay/install.py')
        with tempfile.TemporaryDirectory() as tmp:
            with patch.object(installer,'UNIT',Path(tmp)/'missing.service'),patch.object(sys,'argv',['install.py','--update-only']),patch.object(installer,'run',side_effect=AssertionError('must not change system')):
                installer.main()
            self.assertEqual(list(Path(tmp).iterdir()),[])

    def test_unit_quotes_percent_and_spaces(self):
        installer=module('overlay_installer_quotes',ROOT/'scripts/overlay/install.py')
        self.assertEqual(installer.quote('/a b/100%/"c"'),'"/a b/100%%/\\"c\\""')

if __name__=='__main__':unittest.main(verbosity=2)
