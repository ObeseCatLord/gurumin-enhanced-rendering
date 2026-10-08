#!/usr/bin/env python3
"""Round-trip tests using stock sidecars in a research-only temporary directory."""
import importlib.util
import json
import os
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch
from contextlib import redirect_stdout
from io import StringIO
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
STOCK = Path(os.environ['GURUMIN_STOCK_DIR']) if os.environ.get('GURUMIN_STOCK_DIR') else None
SPEC = importlib.util.spec_from_file_location('gurumin_install', ROOT / 'tools/install.py')
installer = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(installer)


@unittest.skipUnless(STOCK, 'Set GURUMIN_STOCK_DIR to your owned Steam game for stock-binary integration tests')
class InstallTest(unittest.TestCase):
 def setUp(self):
  self.temp = tempfile.TemporaryDirectory()
  self.game = Path(self.temp.name) / 'game'
  self.game.mkdir()
  for name in ('game.exe', 'gurumin.exe'):
   original = STOCK / f'{name}.guruminfix-original'
   if not original.exists():
    original = STOCK / name
   self.assertEqual(installer.sha(original.read_bytes()), installer.ORIGINAL)
   shutil.copy2(original, self.game / name)
  self.original = {name: (self.game / name).read_bytes() for name in ('game.exe', 'gurumin.exe')}

 def tearDown(self):
  self.temp.cleanup()

 def run_install(self, width=3440, height=1440):
  with redirect_stdout(StringIO()):
   installer.install(self.game, width, height)

 def test_install_reinstall_and_restore_round_trip(self):
  self.run_install()
  for name in ('game.exe', 'gurumin.exe'):
   data = (self.game / name).read_bytes()
   self.assertEqual(struct.unpack_from('<I', data, installer.MODE_WIDTH)[0], 3440)
   self.assertEqual(struct.unpack_from('<I', data, installer.MODE_HEIGHT)[0], 1440)
   self.assertEqual(struct.unpack_from('<I', data, installer.INTERNAL_EDGE)[0], 3440)
   self.assertEqual(struct.unpack_from('<I', data, installer.TEXTURE_WIDTH_LIMIT)[0], 3440)
   self.assertEqual(struct.unpack_from('<I', data, installer.TEXTURE_HEIGHT_LIMIT)[0], 3440)
  self.run_install(1600, 1200)
  for name in ('game.exe', 'gurumin.exe'):
   data = (self.game / name).read_bytes()
   self.assertEqual(struct.unpack_from('<I', data, installer.MODE_WIDTH)[0], 1600)
   self.assertEqual(struct.unpack_from('<I', data, installer.MODE_HEIGHT)[0], 1200)
   self.assertEqual(struct.unpack_from('<I', data, installer.INTERNAL_EDGE)[0], 1600)
  installer.restore(self.game)
  for name, original in self.original.items():
   self.assertEqual((self.game / name).read_bytes(), original)
   self.assertEqual(installer.sha((self.game / name).read_bytes()), installer.sha(original))
  self.assertFalse((self.game / 'd3d9.dll').exists())
  self.assertFalse((self.game / installer.INI).exists())

 def test_restore_retry_and_reinstall(self):
  self.run_install()
  installer.restore(self.game)
  installer.restore(self.game)
  self.run_install(2560, 1440)
  self.assertTrue((self.game/'d3d9.dll').exists())
  self.assertEqual(json.loads((self.game/'GuruminModern-backup/manifest.json').read_text())['state'], 'installed')

 def test_steam_validation_then_restore(self):
  self.run_install()
  for name,data in self.original.items():
   (self.game/name).write_bytes(data)
  installer.restore(self.game)
  self.assertFalse((self.game/'d3d9.dll').exists())
  for name,data in self.original.items():
   self.assertEqual((self.game/name).read_bytes(),data)

 def test_license_notice_owned_migration_and_unowned_refusal(self):
  self.run_install()
  notice = self.game / installer.NOTICE
  self.assertEqual(notice.read_bytes(), (ROOT / 'dist' / installer.NOTICE).read_bytes())
  manifest = self.game / 'GuruminModern-backup' / 'manifest.json'
  data = json.loads(manifest.read_text())
  data['files'].pop(installer.NOTICE)
  manifest.write_text(json.dumps(data))
  before = manifest.read_bytes()
  with self.assertRaisesRegex(SystemExit, 'unowned file already exists'):
   self.run_install()
  self.assertEqual(manifest.read_bytes(), before)
  notice.unlink()
  self.run_install()
  self.assertIn(installer.NOTICE, json.loads(manifest.read_text())['files'])
  installer.restore(self.game)
  self.assertFalse(notice.exists())

 def test_owned_ini_is_preserved_on_reinstall_and_restore(self):
  template = ROOT / 'dist' / installer.INI
  if not template.exists():
   self.skipTest('INI template has not been built yet')
  self.run_install()
  config = self.game / installer.INI
  config.write_text('flags=1\nuser_setting=keep\n')
  self.run_install(1600, 1200)
  self.assertEqual(config.read_text(), 'flags=1\nuser_setting=keep\n')
  output = StringIO()
  with redirect_stdout(output):
   installer.restore(self.game)
  self.assertEqual(config.read_text(), 'flags=1\nuser_setting=keep\n')
  self.assertIn('preserved in place', output.getvalue())

 def test_legacy_manifest_migrates_ini_only_when_no_unowned_ini_exists(self):
  template = ROOT / 'dist' / installer.INI
  if not template.exists():
   self.skipTest('INI template has not been built yet')
  self.run_install()
  manifest = self.game / 'GuruminModern-backup' / 'manifest.json'
  legacy = json.loads(manifest.read_text())
  legacy['files'].pop(installer.INI)
  manifest.write_text(json.dumps(legacy, indent=2))
  config = self.game / installer.INI
  config.write_text('unowned=1\n')
  before = {name: (self.game / name).read_bytes() for name in ('game.exe', 'gurumin.exe', 'd3d9.dll', installer.INI)}
  old_manifest = manifest.read_bytes()
  with self.assertRaisesRegex(SystemExit, 'unowned file already exists'):
   self.run_install(1600, 1200)
  self.assertEqual(before, {name: (self.game / name).read_bytes() for name in before})
  self.assertEqual(manifest.read_bytes(), old_manifest)
  config.unlink()
  self.run_install(1600, 1200)
  migrated = json.loads(manifest.read_text())
  self.assertIn(installer.INI, migrated['files'])
  self.assertEqual(config.read_bytes(), template.read_bytes().replace(b'Width=0',b'Width=1600').replace(b'Height=0',b'Height=1200'))

 def test_legacy_three_file_manifest_restores(self):
  self.run_install()
  manifest = self.game / 'GuruminModern-backup' / 'manifest.json'
  legacy = json.loads(manifest.read_text())
  legacy['files'].pop(installer.INI, None)
  for name in ('game.exe', 'gurumin.exe'):
   legacy['files'][name].pop('backup_sha256', None)
  manifest.write_text(json.dumps(legacy, indent=2))
  (self.game / installer.INI).unlink(missing_ok=True)
  installer.restore(self.game)
  for name, original in self.original.items():
   self.assertEqual((self.game / name).read_bytes(), original)
  self.assertFalse((self.game / 'd3d9.dll').exists())

 def test_unknown_executable_and_modified_dll_refuse_without_writes(self):
  (self.game / 'game.exe').write_bytes(b'unknown')
  before = {p.name: p.read_bytes() for p in self.game.iterdir() if p.is_file()}
  with self.assertRaisesRegex(SystemExit, 'unknown stock executable'):
   self.run_install()
  self.assertEqual(before, {p.name: p.read_bytes() for p in self.game.iterdir() if p.is_file()})
  self.assertFalse((self.game / 'GuruminModern-backup').exists())
  (self.game / 'game.exe').write_bytes(self.original['game.exe'])
  self.run_install()
  dll = self.game / 'd3d9.dll'
  dll.write_bytes(dll.read_bytes() + b'changed')
  before = {p.name: p.read_bytes() for p in self.game.iterdir() if p.is_file()}
  manifest = self.game / 'GuruminModern-backup' / 'manifest.json'
  old_manifest = manifest.read_bytes()
  with self.assertRaisesRegex(SystemExit, 'd3d9.dll changed since installation'):
   self.run_install(1600, 1200)
  self.assertEqual(before, {p.name: p.read_bytes() for p in self.game.iterdir() if p.is_file()})
  self.assertEqual(manifest.read_bytes(), old_manifest)

 def test_corrupted_backup_refuses_restore_without_writes(self):
  self.run_install()
  backup = self.game / 'GuruminModern-backup' / 'game.exe'
  backup.write_bytes(backup.read_bytes() + b'corrupt')
  before = {name: (self.game / name).read_bytes() for name in ('game.exe', 'gurumin.exe', 'd3d9.dll')}
  with self.assertRaisesRegex(SystemExit, 'backup hash mismatch'):
   installer.restore(self.game)
  self.assertEqual(before, {name: (self.game / name).read_bytes() for name in before})

 def test_write_failure_rolls_back_files_and_manifest(self):
  self.run_install()
  manifest = self.game / 'GuruminModern-backup' / 'manifest.json'
  names = ('game.exe', 'gurumin.exe', 'd3d9.dll') + ((installer.INI,) if (self.game / installer.INI).exists() else ())
  before = {name: (self.game / name).read_bytes() for name in names}
  old_manifest = manifest.read_bytes()
  def fail_on_second_exe(path, data):
   if path.name == 'gurumin.exe':
    raise OSError('injected write failure')
   installer.write(path, data)
  with self.assertRaisesRegex(OSError, 'injected write failure'):
   installer.install(self.game, 1600, 1200, writer=fail_on_second_exe)
  self.assertEqual(before, {name: (self.game / name).read_bytes() for name in before})
  self.assertEqual(manifest.read_bytes(), old_manifest)

 def test_keyboard_interrupt_after_each_replacement_rolls_back(self):
  names = ('game.exe', 'gurumin.exe', 'd3d9.dll', installer.INI, installer.NOTICE)
  manifest = self.game/'GuruminModern-backup/manifest.json'
  for updating in (False, True):
   if updating:
    self.run_install()
   baseline = {name: (self.game/name).read_bytes() if (self.game/name).exists() else None for name in names}
   prior = manifest.read_bytes() if manifest.exists() else None
   replaced = tuple(name for name in names if not (updating and name == installer.INI))
   for target, occurrence in [(name,1) for name in replaced]+[('manifest.json',1),('manifest.json',2)]:
    hits = 0
    def interrupt(path, data):
     nonlocal hits
     installer.write(path,data)
     if path.name == target:
      hits+=1
      if hits == occurrence:raise KeyboardInterrupt()
    with self.subTest(updating=updating,target=target,occurrence=occurrence):
     with self.assertRaises(KeyboardInterrupt):
      installer.install(self.game, 2560, 1440, writer=interrupt)
     self.assertEqual(baseline, {name: (self.game/name).read_bytes() if (self.game/name).exists() else None for name in baseline})
     self.assertEqual(manifest.read_bytes() if manifest.exists() else None,prior)

 def test_hard_exit_during_initial_install_can_be_restored(self):
  code = """import sys,os
from pathlib import Path
sys.path.insert(0,sys.argv[1])
import install
real_write=install.write
def killed(path,data):
 real_write(path,data)
 if path.name=='gurumin.exe':os._exit(42)
install.install(Path(sys.argv[2]),2560,1440,writer=killed)
"""
  result=subprocess.run([sys.executable,'-c',code,str(ROOT/'tools'),str(self.game)],capture_output=True)
  self.assertEqual(result.returncode,42)
  manifest=json.loads((self.game/'GuruminModern-backup/manifest.json').read_text())
  self.assertEqual(manifest['state'],'installing')
  installer.restore(self.game)
  for name,data in self.original.items():self.assertEqual((self.game/name).read_bytes(),data)
  self.assertFalse((self.game/'d3d9.dll').exists())

 def test_initial_cancel_with_failed_rollback_can_be_restored(self):
  real_write=installer.write
  def locked_rollback(path,data):
   if path.parent == self.game and path.name == 'game.exe' and data == self.original['game.exe']:
    raise OSError('rollback locked')
   real_write(path,data)
  def interrupt(path,data):
   real_write(path,data)
   if path.parent == self.game and path.name == 'game.exe':raise KeyboardInterrupt()
  with patch.object(installer,'write',side_effect=locked_rollback):
   with self.assertRaisesRegex(RuntimeError,'recovery manifest'):
    installer.install(self.game,2560,1440,writer=interrupt)
  manifest=json.loads((self.game/'GuruminModern-backup/manifest.json').read_text())
  self.assertEqual(manifest['state'],'installing')
  self.run_install(1920,1080)
  installer.restore(self.game)
  self.assertEqual((self.game/'game.exe').read_bytes(),self.original['game.exe'])

 def test_optional_installer_adopts_exact_extracted_payload(self):
  for name in ('d3d9.dll',installer.NOTICE):
   shutil.copy2(ROOT/'dist'/name,self.game/name)
  config=self.game/installer.INI
  config.write_text('[GuruminModern]\nFrameCap=175\nFreeCamera=0\n')
  before=config.read_bytes()
  self.run_install()
  self.assertEqual(config.read_bytes(),before)
  self.run_install(2560,1440)
  self.assertEqual(config.read_bytes(),before)
  installer.restore(self.game)
  self.assertFalse((self.game/'d3d9.dll').exists())
  self.assertEqual(config.read_bytes(),before)

 def test_managed_install_then_copied_release_payload(self):
  self.run_install()
  manifest=self.game/'GuruminModern-backup/manifest.json'
  prior=json.loads(manifest.read_text())
  # Simulate a previous release's ownership hash, then a copy of this release.
  for name in ('d3d9.dll',installer.NOTICE):
   prior['files'][name]['installed_sha256']=installer.sha(b'previous release')
   prior['files'][name]['previous_sha256']=None
   shutil.copy2(ROOT/'dist'/name,self.game/name)
  manifest.write_text(json.dumps(prior))
  self.run_install(2560,1440)
  installer.restore(self.game)
  self.assertFalse((self.game/'d3d9.dll').exists())
  # Restore also recognizes the current bundled files without reinstalling.
  self.run_install()
  manifest.write_text(json.dumps(prior))
  installer.restore(self.game)
  self.assertFalse((self.game/'d3d9.dll').exists())

 def test_status_reports_manifest_and_dll_errors(self):
  self.run_install()
  (self.game / 'd3d9.dll').unlink()
  (self.game / 'GuruminModern-backup' / 'manifest.json').write_text('{')
  output = StringIO()
  with redirect_stdout(output):
   installer.status(self.game)
  self.assertIn('manifest: error:', output.getvalue())
  self.assertIn('d3d9.dll: missing', output.getvalue())
  if (ROOT / 'dist' / installer.INI).exists():
   self.assertIn(f'{installer.INI}:', output.getvalue())
   self.assertNotIn('file is too short', output.getvalue())


if __name__ == '__main__':
 unittest.main()
