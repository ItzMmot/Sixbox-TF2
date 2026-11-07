# tests/build_local_dll_test.py - cache invalidation and safe DLL installation.
import contextlib
import importlib.util
import io
from pathlib import Path
import struct
import tempfile
import unittest
from unittest.mock import patch

SPEC = importlib.util.spec_from_file_location('sixbox_local_build', Path(__file__).resolve().parents[1] / '.github/scripts/build-local-dll.py')
BUILD = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(BUILD)


def image(import_name=b'kernel32.dll', marker=0):
    data = bytearray(1024)
    data[:2] = b'MZ'
    struct.pack_into('<I', data, 0x3c, 0x80)
    data[0x80:0x84] = b'PE\0\0'
    struct.pack_into('<HHIIIHH', data, 0x84, 0x8664, 1, 0, 0, 0, 240, 0x2000)
    opt = 0x98
    struct.pack_into('<H', data, opt, 0x20b)
    struct.pack_into('<I', data, opt + 16, 0x1000)
    struct.pack_into('<I', data, opt + 56, 0x2000)
    struct.pack_into('<I', data, opt + 120, 0x1000)
    struct.pack_into('<IIII', data, opt + 240 + 8, 512, 0x1000, 512, 512)
    struct.pack_into('<I', data, 512 + 12, 0x1060)
    data[608:608 + len(import_name)] = import_name
    data[800] = marker
    return bytes(data)


class LocalBuildTest(unittest.TestCase):
    def test_source_and_configuration_changes_invalidate_cache(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            src = root / 'src'; src.mkdir()
            header = src / 'feature.h'; header.write_text('before')
            cpp = src / 'feature.cpp'; cpp.write_text('body')
            project = root / 'project'; project.write_text('list')
            def key(flags='AVX2'):
                return BUILD.snapshot(root, [src], [project], {'flags': flags})[0]
            before = key()
            self.assertEqual(before, key())
            # Contents matter even when an editor preserves the timestamp.
            old_time = header.stat().st_mtime_ns
            header.write_text('after!')
            import os
            os.utime(header, ns=(old_time, old_time))
            self.assertNotEqual(before, key())
            header.write_text('before'); self.assertEqual(before, key())
            extra = src / 'new.h'; extra.write_text('new')
            self.assertNotEqual(before, key())
            extra.unlink(); self.assertEqual(before, key())
            cpp.unlink(); self.assertNotEqual(before, key())
            cpp.write_text('body'); self.assertEqual(before, key())
            project.write_text('new-list'); self.assertNotEqual(before, key())
            project.write_text('list'); self.assertNotEqual(before, key('other-flags'))

    def test_project_sources_follow_additions_and_optional_files(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / 'a.cpp').write_text('')
            project = root / 'test.vcxproj'
            project.write_text('''<Project><ItemGroup><ClCompile Include="a.cpp"/><ClCompile Include="b.cpp" Condition="Exists('b.cpp')"/></ItemGroup></Project>''')
            self.assertEqual(BUILD.project_sources(project), [(root / 'a.cpp').resolve()])
            (root / 'b.cpp').write_text('')
            self.assertEqual(len(BUILD.project_sources(project)), 2)
            (root / 'a.cpp').unlink()
            with self.assertRaises(RuntimeError): BUILD.project_sources(project)

    def test_wine_case_preserving_output_rename(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory); expected = root / 'SixBox.dll'
            self.assertEqual(BUILD.normalize_output_case(expected), expected)
            old = root / 'Sixbox.dll'; old.write_bytes(b'linked DLL')
            BUILD.normalize_output_case(expected)
            self.assertFalse(old.exists())
            self.assertEqual(expected.read_bytes(), b'linked DLL')
            BUILD.normalize_output_case(expected)
            old.write_bytes(b'another DLL')
            with self.assertRaises(RuntimeError): BUILD.normalize_output_case(expected)
            self.assertEqual(old.read_bytes(), b'another DLL')
            self.assertEqual(expected.read_bytes(), b'linked DLL')

    def test_release_pe_validation(self):
        self.assertEqual(BUILD.pe_imports(image()), ['kernel32.dll'])
        for name in [b'msvcp140d.dll', b'vcruntime140d.dll', b'ucrtbased.dll', b'freetype.dll']:
            with self.assertRaises(RuntimeError): BUILD.pe_imports(image(name))
        wrong = bytearray(image()); struct.pack_into('<H', wrong, 0x84, 0x14c)
        with self.assertRaises(RuntimeError): BUILD.pe_imports(wrong)
        for invalid in [b'not a DLL', image()[:100], image()[:520]]:
            with self.assertRaises((RuntimeError, struct.error)): BUILD.pe_imports(invalid)

    def test_backup_and_install_and_unchanged_reinstall(self):
        with tempfile.TemporaryDirectory() as directory, contextlib.redirect_stdout(io.StringIO()):
            root = Path(directory); source = root / 'new.dll'; target = root / 'installed.dll'
            source.write_bytes(image(marker=2)); target.write_bytes(image(marker=1))
            receipt = BUILD.install(source, target, root / 'backups')
            self.assertEqual(target.read_bytes(), source.read_bytes())
            self.assertEqual(Path(receipt['backup']).read_bytes(), image(marker=1))
            BUILD.install(source, target, root / 'backups')
            self.assertEqual(len(list((root / 'backups').glob('*.dll'))), 1)

    def test_renamed_launcher_preserves_steam_command_and_backup(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory); backups = root / 'backups'
            legacy = root / 'inject-amalgam-offline.bat'
            original = b'original launcher'
            legacy.write_bytes(original)
            BUILD.install_launchers(root, backups)
            renamed = root / 'inject-sixbox-offline.bat'
            self.assertIn('SixBoxx64ReleaseFreetypeAVX2.dll', renamed.read_text())
            self.assertIn('inject-sixbox-offline.bat', legacy.read_text())
            self.assertEqual(len(list(backups.glob('*.bat'))), 1)
            self.assertEqual(next(backups.glob('*.bat')).read_bytes(), original)
            BUILD.install_launchers(root, backups)
            self.assertEqual(len(list(backups.glob('*.bat'))), 1)
            self.assertEqual(list(root.glob('.sixbox-launcher-*')), [])

    def test_failed_launcher_replace_keeps_existing_command(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            legacy = root / 'inject-amalgam-offline.bat'
            legacy.write_bytes(b'original')
            with patch.object(BUILD.os, 'replace', side_effect=PermissionError('test')):
                with self.assertRaises(PermissionError):
                    BUILD.install_launchers(root, root / 'backups')
            self.assertEqual(legacy.read_bytes(), b'original')
            self.assertEqual(list(root.glob('.sixbox-launcher-*')), [])

    def test_rejected_or_failed_install_preserves_previous_dll(self):
        with tempfile.TemporaryDirectory() as directory, contextlib.redirect_stdout(io.StringIO()):
            root = Path(directory); source = root / 'new.dll'; target = root / 'installed.dll'
            target.write_bytes(image(marker=1)); source.write_bytes(b'invalid')
            with self.assertRaises(RuntimeError): BUILD.install(source, target, root / 'backups')
            self.assertEqual(target.read_bytes(), image(marker=1))
            source.write_bytes(image(marker=2))
            with patch.object(BUILD.os, 'replace', side_effect=PermissionError('test')):
                with self.assertRaises(PermissionError): BUILD.install(source, target, root / 'backups')
            self.assertEqual(target.read_bytes(), image(marker=1))
            self.assertEqual(list(root.glob('.sixbox-*')), [])


if __name__ == '__main__':
    unittest.main()
