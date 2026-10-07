"""Bounded Steam discovery for Gurumin Enhanced Rendering.

This module deliberately identifies an installation by Steam's app manifest and
the two executables needed by the installer.  It does not establish whether an
executable is a supported stock build; that remains the installer's job.
"""
from __future__ import annotations

from dataclasses import dataclass
import os
import shlex
from pathlib import Path
from typing import Iterable, List, Tuple, Union
from urllib.parse import unquote, urlsplit


APP_ID = "322290"
GAME_EXES = ("game.exe", "gurumin.exe")


@dataclass(frozen=True)
class Installation:
 """A Gurumin installation located in one Steam library."""

 game: Path
 steam_root: Path
 library: Path


class _VdfError(ValueError):
 pass


@dataclass(frozen=True)
class _VdfToken:
 kind: str
 value: str
 start: int
 end: int


@dataclass
class _VdfEntry:
 key: str
 value: Union[str, List["_VdfEntry"]]
 key_span: Tuple[int, int]
 value_span: Tuple[int, int]
 content_span: Union[Tuple[int, int], None] = None
 close_brace: Union[int, None] = None


def _token_spans(text: str) -> Iterable[_VdfToken]:
 """Yield KeyValues tokens with source spans, including quotes and braces."""
 index = 0
 length = len(text)
 while index < length:
  char = text[index]
  if char.isspace():
   index += 1
   continue
  if char == "/" and index + 1 < length and text[index + 1] == "/":
   newline = text.find("\n", index + 2)
   index = length if newline == -1 else newline + 1
   continue
  if char in "{}":
   yield _VdfToken("brace", char, index, index + 1)
   index += 1
   continue
  if char != '"':
   raise _VdfError("expected a quoted KeyValues string")
  start = index
  index += 1
  value: List[str] = []
  while index < length:
   char = text[index]
   if char == '"':
    index += 1
    yield _VdfToken("string", "".join(value), start, index)
    break
   if char == "\\" and index + 1 < length:
    escaped = text[index + 1]
    # Steam escapes quote and backslash.  Preserve an unrecognised escape so a
    # hand-written Windows path such as C:\Steam is not corrupted.
    if escaped in ('"', "\\"):
     value.append(escaped)
    else:
     value.extend(("\\", escaped))
    index += 2
    continue
   value.append(char)
   index += 1
  else:
   raise _VdfError("unterminated KeyValues string")


def _tokens(text: str) -> Iterable[Tuple[str, str]]:
 """Yield quoted strings and braces from a Valve KeyValues document."""
 for token in _token_spans(text):
  yield token.kind, token.value


def _parse_vdf_document(text: str) -> List[_VdfEntry]:
 """Parse KeyValues once, retaining editable quoted-value source spans."""
 tokens = list(_token_spans(text))
 position = 0

 def entries(stop_at_brace: bool = False) -> Tuple[List[_VdfEntry], Union[int, None]]:
  nonlocal position
  result: List[_VdfEntry] = []
  while position < len(tokens):
   token = tokens[position]
   if token.kind == "brace" and token.value == "}":
    if not stop_at_brace:
     raise _VdfError("unexpected closing brace")
    position += 1
    return result, token.start
   if token.kind != "string":
    raise _VdfError("expected KeyValues key")
   key = token
   position += 1
   if position >= len(tokens):
    raise _VdfError("missing KeyValues value")
   value = tokens[position]
   position += 1
   if value.kind == "string":
    result.append(_VdfEntry(key.value, value.value, (key.start, key.end),
                            (value.start, value.end), (value.start + 1, value.end - 1)))
   elif value.kind == "brace" and value.value == "{":
    children, close_brace = entries(True)
    result.append(_VdfEntry(key.value, children, (key.start, key.end),
                            (value.start, value.end), close_brace=close_brace))
   else:
    raise _VdfError("expected KeyValues value")
  if stop_at_brace:
   raise _VdfError("missing closing brace")
  return result, None

 parsed, _ = entries()
 if position != len(tokens):
  raise _VdfError("trailing KeyValues data")
 return parsed


def _parse_vdf(text: str) -> List[Tuple[str, Union[str, list]]]:
 """Parse the small quoted KeyValues grammar used by Steam VDF files."""
 def plain(entries: List[_VdfEntry]) -> List[Tuple[str, Union[str, list]]]:
  return [(entry.key, entry.value if isinstance(entry.value, str) else plain(entry.value))
          for entry in entries]
 return plain(_parse_vdf_document(text))


def _values(entries: List[Tuple[str, Union[str, list]]], key: str) -> List[Union[str, list]]:
 return [value for entry_key, value in entries if entry_key.casefold() == key.casefold()]


def _library_paths(vdf: Path) -> List[Path]:
 try:
  document = _parse_vdf(vdf.read_text(encoding="utf-8-sig"))
 except (OSError, UnicodeError, _VdfError):
  return []
 paths: List[Path] = []

 def visit(entries: List[Tuple[str, Union[str, list]]], allow_legacy: bool = False) -> None:
  for key, value in entries:
   if key.casefold() == "path" and isinstance(value, str):
    paths.append(Path(value))
   elif allow_legacy and key.isdecimal() and isinstance(value, str):
    # Older libraryfolders.vdf files use "1" "/library/path".
    paths.append(Path(value))
   elif isinstance(value, list):
    visit(value)

 for library_folders in _values(document, "libraryfolders"):
  if isinstance(library_folders, list):
   visit(library_folders, allow_legacy=True)
 return paths


def _string_value(entries: List[Tuple[str, Union[str, list]]], key: str) -> Union[str, None]:
 for entry_key, value in entries:
  if entry_key.casefold() == key.casefold() and isinstance(value, str):
   return value
 return None


def _manifest_game(manifest: Path, library: Path) -> Union[Path, None]:
 try:
  document = _parse_vdf(manifest.read_text(encoding="utf-8-sig"))
 except (OSError, UnicodeError, _VdfError):
  return None
 app_states = _values(document, "appstate")
 if len(app_states) != 1 or not isinstance(app_states[0], list):
  return None
 app_state = app_states[0]
 if _string_value(app_state, "appid") != APP_ID:
  return None
 install_dir = _string_value(app_state, "installdir")
 if not install_dir:
  return None
 common = library / "steamapps" / "common"
 try:
  common_resolved = common.resolve()
  game = (common / install_dir).resolve()
  game.relative_to(common_resolved)
 except (OSError, ValueError):
  return None
 if not game.is_dir() or not all((game / name).is_file() for name in GAME_EXES):
  return None
 return game


def _default_roots() -> List[Path]:
 roots: List[Path] = []
 steam_path = os.environ.get("STEAM_PATH")
 if steam_path:
  roots.extend(Path(part).expanduser() for part in steam_path.split(os.pathsep) if part)
 if os.name == "nt":
  try:
   import winreg
   for hive, subkey, value_name in (
       (winreg.HKEY_CURRENT_USER, r"Software\Valve\Steam", "SteamPath"),
       (winreg.HKEY_LOCAL_MACHINE, r"Software\Valve\Steam", "InstallPath"),
       (winreg.HKEY_LOCAL_MACHINE, r"Software\Wow6432Node\Valve\Steam", "InstallPath"),
   ):
    try:
     with winreg.OpenKey(hive, subkey) as key:
      value, _ = winreg.QueryValueEx(key, value_name)
     if isinstance(value, str) and value:
      roots.append(Path(value))
    except OSError:
     pass
  except ImportError:
   pass
  for variable in ("ProgramFiles(x86)", "ProgramFiles"):
   if os.environ.get(variable):
    roots.append(Path(os.environ[variable]) / "Steam")
 else:
  home = Path.home()
  roots.extend((
      home / ".local" / "share" / "Steam",
      home / ".steam" / "root",
      home / ".steam" / "steam",
      home / ".var" / "app" / "com.valvesoftware.Steam" / "data" / "Steam",
  ))
 return roots


def _path_key(path: Path) -> Path:
 try:
  return path.expanduser().resolve()
 except OSError:
  return path.expanduser().absolute()


def discover(roots: Union[Iterable[Union[str, os.PathLike]], None] = None) -> List[Installation]:
 """Find valid app 322290 installations below the given Steam roots.

 Library folders are followed recursively because Steam installations can carry
 a libraryfolders.vdf in a nested root.  The first root that reaches a library
 owns its reported ``steam_root``; duplicate physical paths are scanned once.
 """
 seed_paths = _default_roots() if roots is None else [Path(root) for root in roots]
 queue: List[Tuple[Path, Path]] = []
 seen_sources = set()
 seen_libraries = set()
 libraries: List[Tuple[Path, Path]] = []
 for root in seed_paths:
  key = _path_key(root)
  if key not in seen_sources:
   seen_sources.add(key)
   queue.append((key, key))
 while queue:
  source, steam_root = queue.pop(0)
  if not source.is_dir():
   continue
  source_key = _path_key(source)
  if source_key not in seen_libraries:
   seen_libraries.add(source_key)
   libraries.append((source_key, steam_root))
  for library in _library_paths(source_key / "steamapps" / "libraryfolders.vdf"):
   library_key = _path_key(library)
   if library_key not in seen_libraries:
    seen_libraries.add(library_key)
    libraries.append((library_key, steam_root))
   if library_key not in seen_sources:
    seen_sources.add(library_key)
    queue.append((library_key, steam_root))
 installations: List[Installation] = []
 for library, steam_root in libraries:
  game = _manifest_game(library / "steamapps" / ("appmanifest_" + APP_ID + ".acf"), library)
  if game is not None:
   installations.append(Installation(game=game, steam_root=steam_root, library=library))
 return installations


def _file_uri_path(value: str) -> Path:
 uri = urlsplit(value)
 if uri.scheme.casefold() != "file":
  raise ValueError("path must be a local path or file: URI")
 if uri.query or uri.fragment:
  raise ValueError("file URI must not contain a query or fragment")
 decoded = unquote(uri.path)
 if uri.netloc and uri.netloc.casefold() != "localhost":
  decoded = "//" + uri.netloc + decoded
 if os.name == "nt" and len(decoded) >= 3 and decoded[0] == "/" and decoded[2] == ":":
  decoded = decoded[1:]
 return Path(decoded)


def resolve_game(path: Union[str, os.PathLike]) -> Path:
 """Resolve a dropped game directory, game.exe, or gurumin.exe to its root."""
 value = os.fspath(path).strip()
 original = value
 if len(value) >= 2 and value[0] == value[-1] and value[0] in "\"'":
  value = value[1:-1].strip()
 if value.casefold().startswith("file:"):
  candidate = _file_uri_path(value)
 else:
  candidate = Path(value).expanduser()
  if os.name != "nt" and not candidate.exists():
   # Desktop terminals may paste a shell-escaped path on drag/drop. Parse one
   # literal argument only; do not invoke a shell or expand command syntax.
   try:
    words = shlex.split(original)
    if len(words) == 1:
     candidate = Path(words[0]).expanduser()
   except ValueError:
    pass
 if candidate.is_file():
  if candidate.name.casefold() not in GAME_EXES:
   raise ValueError("drop the Gurumin directory, game.exe, or gurumin.exe")
  candidate = candidate.parent
 if not candidate.is_dir():
  raise ValueError("Gurumin directory does not exist")
 game = _path_key(candidate)
 if not all((game / name).is_file() for name in GAME_EXES):
  raise ValueError("Gurumin directory must contain game.exe and gurumin.exe")
 return game


def prefix_candidates(installation: Installation) -> List[Path]:
 """Return possible Proton prefixes for a discovered Steam installation."""
 paths: List[Path] = []
 seen = set()
 for base in (installation.library, installation.steam_root):
  prefix = _path_key(base) / "steamapps" / "compatdata" / APP_ID / "pfx"
  key = _path_key(prefix)
  if key not in seen:
   seen.add(key)
   paths.append(key)
 return paths
