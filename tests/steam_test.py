#!/usr/bin/env python3
"""Isolated Steam-library fixtures for tools.steam."""
import importlib.util
import sys
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("gurumin_steam", ROOT / "tools" / "steam.py")
steam = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = steam
SPEC.loader.exec_module(steam)


def quote(value):
 return value.replace("\\", "\\\\").replace('"', '\\"')


class SteamTest(unittest.TestCase):
 def setUp(self):
  self.temp = tempfile.TemporaryDirectory()
  self.base = Path(self.temp.name)

 def tearDown(self):
  self.temp.cleanup()

 def root(self, name="Steam Root"):
  path = self.base / name
  (path / "steamapps").mkdir(parents=True)
  return path

 def library_vdf(self, root, paths, legacy=False):
  if legacy:
   records = "\n".join('  "{}" "{}"'.format(index, quote(str(path))) for index, path in enumerate(paths, 1))
  else:
   records = "\n".join('  "{}"\n  {{\n   "path" "{}"\n  }}'.format(index, quote(str(path))) for index, path in enumerate(paths))
  (root / "steamapps" / "libraryfolders.vdf").write_text('"libraryfolders"\n{\n' + records + '\n}\n')

 def install(self, library, app_id=steam.APP_ID, install_dir="Gurumin A Monstrous Adventure", files=True):
  game = library / "steamapps" / "common" / install_dir
  game.mkdir(parents=True)
  if files:
   for name in steam.GAME_EXES:
    (game / name).write_bytes(b"test")
  manifest = library / "steamapps" / ("appmanifest_" + steam.APP_ID + ".acf")
  manifest.write_text('"AppState"\n{{\n "appid" "{}"\n "installdir" "{}"\n}}\n'.format(app_id, quote(install_dir)))
  return game

 def test_modern_vdf_spaced_unicode_library_and_comments(self):
  root = self.root()
  library = self.root("Bíblioteca Steam with spaces")
  self.library_vdf(root, [library])
  game = self.install(library)
  found = steam.discover([root])
  self.assertEqual(found, [steam.Installation(game.resolve(), root.resolve(), library.resolve())])

 def test_legacy_vdf_recursive_roots_and_deduplication(self):
  root = self.root()
  first = self.root("Library One")
  second = self.root("Library Two")
  self.library_vdf(root, [first, first], legacy=True)
  self.library_vdf(first, [second])
  self.install(second)
  found = steam.discover([root, root.resolve()])
  self.assertEqual(len(found), 1)
  self.assertEqual(found[0].game, (second / "steamapps" / "common" / "Gurumin A Monstrous Adventure").resolve())
  self.assertEqual(found[0].library, second.resolve())
  self.assertEqual(found[0].steam_root, root.resolve())

 def test_vdf_escapes_windows_backslashes_and_quoted_comments(self):
  document = steam._parse_vdf('"libraryfolders" { // comment\n "0" { "path" "C:\\\\Steam Library" } }')
  folders = steam._values(document, "libraryfolders")[0]
  self.assertEqual(steam._string_value(steam._values(folders, "0")[0], "path"), r"C:\Steam Library")
  document = steam._parse_vdf('"x" "// is data \\"quoted\\""')
  self.assertEqual(document, [("x", '// is data "quoted"')])

 def test_unknown_app_missing_executables_and_path_traversal_are_rejected(self):
  root = self.root()
  unknown = self.root("Unknown")
  missing = self.root("Missing")
  unsafe = self.root("Unsafe")
  self.library_vdf(root, [unknown, missing, unsafe])
  self.install(unknown, app_id="322291")
  self.install(missing, files=False)
  outside = self.base / "outside"
  outside.mkdir()
  for name in steam.GAME_EXES:
   (outside / name).write_bytes(b"test")
  self.install(unsafe, install_dir="../../outside")
  self.assertEqual(steam.discover([root]), [])

 def test_resolve_game_accepts_folder_executables_file_uri_and_quotes(self):
  game = self.install(self.root())
  self.assertEqual(steam.resolve_game(game), game.resolve())
  self.assertEqual(steam.resolve_game(game / "game.exe"), game.resolve())
  self.assertEqual(steam.resolve_game(' "{}" '.format(game / "gurumin.exe")), game.resolve())
  self.assertEqual(steam.resolve_game(game.as_uri()), game.resolve())
  with self.assertRaisesRegex(ValueError, "must contain"):
   steam.resolve_game(self.base)
  other = self.base / "other.exe"
  other.write_bytes(b"")
  with self.assertRaisesRegex(ValueError, "drop the Gurumin"):
   steam.resolve_game(other)

 def test_prefix_candidates_are_deduplicated(self):
  root = self.root()
  library = self.root("Library")
  installation = steam.Installation(game=self.install(library).resolve(), steam_root=root.resolve(), library=library.resolve())
  expected = [
      library.resolve() / "steamapps" / "compatdata" / steam.APP_ID / "pfx",
      root.resolve() / "steamapps" / "compatdata" / steam.APP_ID / "pfx",
  ]
  self.assertEqual(steam.prefix_candidates(installation), expected)
  same = steam.Installation(game=installation.game, steam_root=library.resolve(), library=library.resolve())
  self.assertEqual(steam.prefix_candidates(same), expected[:1])


if __name__ == "__main__":
 unittest.main()
