#!/usr/bin/env python3
"""Isolated selected-profile fixtures for tools.steam_launch."""
import importlib.util
import json
import sys
import tempfile
import unittest
from unittest.mock import patch
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import steam
import steam_launch


def vdf_quote(value):
 return value.replace("\\", "\\\\").replace('"', '\\"')


class SteamLaunchTest(unittest.TestCase):
 def setUp(self):
  self.temp = tempfile.TemporaryDirectory()
  self.base = Path(self.temp.name)
  self.root = self.base / "Steam Root"
  self.root.mkdir()
  self.backup = self.base / "GuruminModern-backup"
  self.installation = steam.Installation(self.base / "game", self.root, self.root)
  self.old_running = steam_launch.steam_running
  steam_launch.steam_running = lambda root: False

 def tearDown(self):
  steam_launch.steam_running = self.old_running
  self.temp.cleanup()

 def config(self, account="123", options=None, app=True, unicode=True):
  profile = self.root / "userdata" / account / "config"
  profile.mkdir(parents=True)
  launch = '' if options is None else '"LaunchOptions" "{}"'.format(vdf_quote(options))
  apps = '"apps"\n    {\n     "322290"\n     {\n      ' + launch + '\n     }\n    }' if app else ''
  text = ('// keep this comment and Bíblioteca text\n'
          '"UserLocalConfigStore"\n{\n "Software"\n {\n  "Valve"\n  {\n'
          '   "Steam"\n   {\n    "Keep" "unchanged"\n    ' + apps + '\n   }\n  }\n }\n}\n')
  path = profile / "localconfig.vdf"
  path.write_text(text, encoding="utf-8")
  return path

 def login(self, accounts):
  users = []
  for account, recent in accounts:
   users.append('"{}" {{ "MostRecent" "{}" }}'.format(account, recent))
  path = self.root / "config"
  path.mkdir()
  (path / "loginusers.vdf").write_text('"users" { ' + ' '.join(users) + ' }')

 def test_active_profile_merges_unrelated_overrides_and_restores_exact_bytes(self):
  account = "123"
  original_options = 'WINEDLLOVERRIDES="dinput8=n,b" gamemoderun %command% -foo "two words"'
  config = self.config(account, original_options)
  original = config.read_bytes()
  self.login([(str(steam_launch.STEAM_ID64_BASE + int(account)), "1")])
  plan = steam_launch.prepare(self.installation, self.backup)
  steam_launch.apply(plan)
  text = config.read_text()
  self.assertIn('Bíblioteca', text)
  self.assertIn('WINEDLLOVERRIDES=\\"dinput8=n,b;d3d9=n,b\\" gamemoderun %command% -foo', text)
  record = json.loads((self.backup / steam_launch.RECORD).read_text())
  self.assertEqual(record["gameAppId"], steam.APP_ID)
  self.assertEqual(record["profilePath"], str(config.parent.parent))
  steam_launch.restore(self.backup)
  self.assertEqual(config.read_bytes(), original)
  self.assertFalse((self.backup / steam_launch.RECORD).exists())

 def test_existing_wrapper_without_dll_override_gets_required_setting(self):
  prior = 'PROTON_LOG=1 gamemoderun %command% -windowed'
  config = self.config(options=prior)
  plan = steam_launch.prepare(self.installation, self.backup)
  steam_launch.apply(plan)
  self.assertIn('WINEDLLOVERRIDES=\\"d3d9=n,b\\" ' + prior, config.read_text())
  steam_launch.restore(self.backup)
  self.assertIn(prior, config.read_text())

 def test_reinstall_after_user_edit_restores_new_baseline(self):
  config=self.config(options='-old')
  steam_launch.apply(steam_launch.prepare(self.installation,self.backup))
  new='WINEDLLOVERRIDES="dinput8=n,b" PROTON_LOG=1 gamemoderun %command% --user-choice'
  config.write_text(config.read_text().replace(vdf_quote('WINEDLLOVERRIDES="d3d9=n,b" %command% -old'),vdf_quote(new)))
  baseline=config.read_bytes()
  steam_launch.apply(steam_launch.prepare(self.installation,self.backup))
  self.assertIn('dinput8=n,b;d3d9=n,b',config.read_text())
  steam_launch.restore(self.backup)
  self.assertEqual(config.read_bytes(),baseline)

 def test_failed_settings_write_retry_retains_original_baseline(self):
  config=self.config(options='-original')
  baseline=config.read_bytes();real_write=steam_launch.write
  def fail(path,data):
   if path==config:raise OSError('locked config')
   real_write(path,data)
  with patch.object(steam_launch,'write',side_effect=fail):
   with self.assertRaises(OSError):steam_launch.apply(steam_launch.prepare(self.installation,self.backup))
  self.assertTrue((self.backup/steam_launch.RECORD).exists())
  steam_launch.apply(steam_launch.prepare(self.installation,self.backup))
  steam_launch.restore(self.backup)
  self.assertEqual(config.read_bytes(),baseline)

 def test_edit_during_ownership_write_preserves_config_and_record(self):
  config=self.config(options='-original')
  plan=steam_launch.prepare(self.installation,self.backup);real_write=steam_launch.write
  def competing_writer(path,data):
   real_write(path,data)
   if path==plan['record']:config.write_bytes(config.read_bytes()+b'// unrelated concurrent edit\n')
  with patch.object(steam_launch,'write',side_effect=competing_writer):
   with self.assertRaisesRegex(ValueError,'changed while saving'):steam_launch.apply(plan)
  self.assertIn(b'// unrelated concurrent edit',config.read_bytes())
  self.assertNotIn(b'd3d9=n,b',config.read_bytes())
  self.assertTrue(plan['record'].exists())

 def test_edit_during_restore_preparation_preserves_config_and_record(self):
  config=self.config(options=None)
  steam_launch.apply(steam_launch.prepare(self.installation,self.backup))
  real_remove=steam_launch._remove_entry
  def competing_writer(text,entry):
   config.write_bytes(config.read_bytes()+b'// unrelated concurrent edit\n')
   return real_remove(text,entry)
  with patch.object(steam_launch,'_remove_entry',side_effect=competing_writer):
   with self.assertRaisesRegex(ValueError,'changed during restoration'):steam_launch.restore(self.backup)
  self.assertIn(b'// unrelated concurrent edit',config.read_bytes())
  self.assertIn(b'd3d9=n,b',config.read_bytes())
  self.assertTrue((self.backup/steam_launch.RECORD).exists())

 def test_empty_options_and_missing_app_are_created_then_removed(self):
  config = self.config(options=None, app=False)
  original = config.read_bytes()
  plan = steam_launch.prepare(self.installation, self.backup)
  steam_launch.apply(plan)
  self.assertIn('"322290"', config.read_text())
  self.assertIn('WINEDLLOVERRIDES=\\"d3d9=n,b\\" %command%', config.read_text())
  steam_launch.restore(self.backup)
  self.assertNotIn('"LaunchOptions"', config.read_text())
  self.assertIn('"322290"', config.read_text())
  self.assertIn('"Keep" "unchanged"', config.read_text())

 def test_fallback_profile_selection_multiple_profiles_and_explicit_steamid64(self):
  first = self.config("123", "-foo")
  self.assertEqual(steam_launch.prepare(self.installation, self.backup)["config"], first)
  second = self.config("456", "-bar")
  with self.assertRaisesRegex(ValueError, "--steam-user"):
   steam_launch.prepare(self.installation, self.backup)
  plan = steam_launch.prepare(self.installation, self.backup,
                              str(steam_launch.STEAM_ID64_BASE + 456))
  self.assertEqual(plan["config"], second)

 def test_missing_command_treats_text_as_game_arguments(self):
  self.assertEqual(steam_launch.merge_launch_options("-foo \"two words\""),
                   'WINEDLLOVERRIDES="d3d9=n,b" %command% -foo "two words"')
  self.assertEqual(steam_launch.merge_launch_options('WINEDLLOVERRIDES="dinput8=n,b" -foo'),
                   'WINEDLLOVERRIDES="dinput8=n,b;d3d9=n,b" %command% -foo')

 def test_complex_input_fails_closed_without_backup_or_config_write(self):
  config = self.config(options='%command% ; rm -rf never')
  original = config.read_bytes()
  with self.assertRaisesRegex(ValueError, "Unsupported complex"):
   steam_launch.prepare(self.installation, self.backup)
  self.assertEqual(config.read_bytes(), original)
  self.assertFalse((self.backup / steam_launch.RECORD).exists())

 def test_apply_cas_and_idempotency(self):
  config = self.config(options="-foo")
  plan = steam_launch.prepare(self.installation, self.backup)
  config.write_text(config.read_text() + "// external\n")
  with self.assertRaisesRegex(ValueError, "changed during"):
   steam_launch.apply(plan)
  self.assertFalse((self.backup / steam_launch.RECORD).exists())
  plan = steam_launch.prepare(self.installation, self.backup)
  steam_launch.apply(plan)
  once = config.read_bytes()
  steam_launch.apply(steam_launch.prepare(self.installation, self.backup))
  self.assertEqual(config.read_bytes(), once)

 def test_restore_preserves_later_user_edit(self):
  config = self.config(options="-foo")
  steam_launch.apply(steam_launch.prepare(self.installation, self.backup))
  text = config.read_text().replace('%command% -foo', '%command% --user-choice')
  config.write_text(text)
  steam_launch.restore(self.backup)
  self.assertIn("--user-choice", config.read_text())
  self.assertFalse((self.backup / steam_launch.RECORD).exists())
  # A later install now treats the preserved user value as its baseline.
  self.assertEqual(steam_launch.prepare(self.installation, self.backup)["metadata"]["before"],
                   'WINEDLLOVERRIDES="d3d9=n,b" %command% --user-choice')

 def test_restore_missing_original_option_survives_steam_reformat_and_new_app_fields(self):
  config = self.config(options=None, app=False)
  steam_launch.apply(steam_launch.prepare(self.installation, self.backup))
  text = config.read_text()
  # Steam may rewrite indentation and add app metadata after our install.
  text = text.replace('"LaunchOptions"', '"NativeApp" "1"\n      "LaunchOptions"')
  config.write_text(text)
  steam_launch.restore(self.backup)
  restored = config.read_text()
  self.assertNotIn('"LaunchOptions"', restored)
  self.assertIn('"NativeApp" "1"', restored)
  self.assertFalse((self.backup / steam_launch.RECORD).exists())

 def test_scoped_running_steam_check_uses_only_executable_under_root(self):
  proc = self.base / "proc"
  executable = self.root / "ubuntu12_32" / "steam"
  executable.parent.mkdir()
  executable.write_bytes(b"x")
  process = proc / "100"
  process.mkdir(parents=True)
  (process / "exe").symlink_to(executable)
  self.assertTrue(self.old_running(self.root, proc))
  outside = self.base / "outside"
  outside.write_bytes(b"x")
  (process / "exe").unlink()
  (process / "exe").symlink_to(outside)
  self.assertFalse(self.old_running(self.root, proc))

 def test_invalid_backup_cannot_target_arbitrary_file(self):
  self.backup.mkdir()
  (self.backup / steam_launch.RECORD).write_text(json.dumps({"version": 1, "gameAppId": steam.APP_ID,
      "steamRoot": str(self.root), "profilePath": str(self.base / "not-userdata" / "123"),
      "existed": True, "before": "x", "expected": "y", "createdText": ""}))
  with self.assertRaisesRegex(ValueError, "Invalid Steam"):
   steam_launch.restore(self.backup)


if __name__ == "__main__":
 unittest.main()
