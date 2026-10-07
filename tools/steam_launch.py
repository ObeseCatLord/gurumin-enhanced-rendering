"""Reversible Steam launch-options setup for Gurumin on Linux.

Only the selected Steam account's app 322290 LaunchOptions value is touched.
The VDF reader comes from :mod:`steam`, so edits retain every unowned branch,
comment, and byte outside the one value or insertion owned by this module.
"""
from __future__ import annotations

import json
import os
from pathlib import Path
from typing import List, Optional, Tuple

try:  # ``patch.py`` runs from tools; tests may import this as tools.steam_launch.
 from install import write
 import steam
except ModuleNotFoundError:  # pragma: no cover - import layout only
 from tools.install import write
 from tools import steam


APP_ID = steam.APP_ID
RECORD = "steam-launch-options.json"
STEAM_ID64_BASE = 76561197960265728
_TARGET = 'WINEDLLOVERRIDES="d3d9=n,b" %command%'


def _quote(value: str) -> str:
 """Return one KeyValues string, retaining Unicode and escaping only VDF syntax."""
 return '"' + value.replace("\\", "\\\\").replace('"', '\\"') + '"'


def _read_vdf(path: Path) -> Tuple[bytes, str, bool]:
 raw = path.read_bytes()
 bom = raw.startswith(b"\xef\xbb\xbf")
 try:
  text = raw[3:].decode("utf-8") if bom else raw.decode("utf-8")
 except UnicodeDecodeError as exc:
  raise ValueError("Steam localconfig.vdf is not UTF-8") from exc
 return raw, text, bom


def _encode_vdf(text: str, bom: bool) -> bytes:
 return (b"\xef\xbb\xbf" if bom else b"") + text.encode("utf-8")


def _unique(entries: List[steam._VdfEntry], key: str) -> Optional[steam._VdfEntry]:
 matches = [entry for entry in entries if entry.key.casefold() == key.casefold()]
 if len(matches) > 1:
  raise ValueError("Duplicate Steam VDF branch: " + key)
 return matches[0] if matches else None


def _branch(entry: Optional[steam._VdfEntry], key: str) -> List[steam._VdfEntry]:
 if entry is None or not isinstance(entry.value, list):
  raise ValueError("Unrecognized Steam localconfig.vdf structure at " + key)
 return entry.value


def _indent(text: str, position: int) -> str:
 line_start = text.rfind("\n", 0, position) + 1
 prefix = text[line_start:position]
 return prefix if prefix.strip() == "" else ""


def _append_object(text: str, parent: steam._VdfEntry, value: str) -> Tuple[str, str]:
 """Append ``value`` before a parsed branch's close brace and return its exact text."""
 if parent.close_brace is None:
  raise ValueError("Unclosed Steam VDF branch")
 base = _indent(text, parent.close_brace)
 unit = "\t" if "\t" in text else " "
 line_start = text.rfind("\n", 0, parent.close_brace) + 1
 if text[line_start:parent.close_brace].strip() == "":
  insertion = base + unit + value + "\n"
  return text[:line_start] + insertion + text[line_start:], insertion
 # Compact VDF puts a closing brace on the same line as another token.
 insertion = "\n" + base + unit + value + "\n" + base
 return text[:parent.close_brace] + insertion + text[parent.close_brace:], insertion


def _new_tree(names: List[str], launch_options: str, indent: str, unit: str) -> str:
 """Make a small KeyValues branch ending in LaunchOptions."""
 if not names:
  return '"LaunchOptions" ' + _quote(launch_options)
 name = names[0]
 body = _new_tree(names[1:], launch_options, indent + unit, unit)
 return '"{}"\n{}{{\n{}{}\n{}}}'.format(name, indent, indent + unit, body, indent)


def _option_entry(text: str, document: List[steam._VdfEntry]):
 root = _unique(document, "UserLocalConfigStore")
 software = _unique(_branch(root, "UserLocalConfigStore"), "Software")
 valve = _unique(_branch(software, "Software"), "Valve")
 steam_branch = _unique(_branch(valve, "Valve"), "Steam")
 return steam_branch


def _replace_or_insert(text: str, launch_options: str):
 """Return edited VDF text and metadata for an owned LaunchOptions update."""
 document = steam._parse_vdf_document(text)
 steam_branch = _option_entry(text, document)
 apps = _unique(_branch(steam_branch, "Steam"), "apps")
 app = _unique(_branch(apps, "apps"), APP_ID) if apps is not None else None
 option = _unique(_branch(app, APP_ID), "LaunchOptions") if app is not None else None
 if option is not None:
  if not isinstance(option.value, str) or option.value_span is None:
   raise ValueError("Steam LaunchOptions is not a string")
  replacement = _quote(launch_options)
  return (text[:option.value_span[0]] + replacement + text[option.value_span[1]:],
          True, option.value, "")

 unit = "\t" if "\t" in text else " "
 if app is not None:
  updated, created = _append_object(text, app, '"LaunchOptions" ' + _quote(launch_options))
 elif apps is not None:
  created_body = _new_tree([APP_ID], launch_options,
                           _indent(text, apps.close_brace or 0) + unit, unit)
  updated, created = _append_object(text, apps, created_body)
 elif steam_branch is not None:
  created_body = _new_tree(["apps", APP_ID], launch_options,
                           _indent(text, steam_branch.close_brace or 0) + unit, unit)
  updated, created = _append_object(text, steam_branch, created_body)
 else:  # Defensive: _option_entry already rejects a missing Steam branch.
  raise ValueError("Unrecognized Steam localconfig.vdf structure at Steam")
 return updated, False, None, created


def _remove_entry(text: str, entry: steam._VdfEntry) -> str:
 """Remove one parsed key/value entry without making assumptions about its parent."""
 start = entry.key_span[0]
 end = entry.value_span[1]
 line_start = text.rfind("\n", 0, start) + 1
 line_end = text.find("\n", end)
 if line_end == -1:
  line_end = len(text)
  newline_end = line_end
 else:
  newline_end = line_end + 1
 # The adapter normally created a complete line.  Removing that line preserves
 # every surrounding branch, including fields Steam later adds to the app.
 if not text[line_start:start].strip() and not text[end:line_end].strip():
  return text[:line_start] + text[newline_end:]
 # Hand-written compact VDF can place entries on a shared line.  Remove only
 # the two owned tokens in that case, leaving comments and sibling tokens.
 return text[:start] + text[end:]


def _shell_words(value: str):
 """Tokenize a deliberately small, non-executing launch-options grammar."""
 words = []
 index = 0
 length = len(value)
 while index < length:
  while index < length and value[index].isspace():
   index += 1
  if index == length:
   break
  start = index
  cooked = []
  quote = None
  while index < length:
   char = value[index]
   if quote is None:
    if char.isspace():
     break
    if char in ";|&<>`":
     raise ValueError("Unsupported complex Steam launch options")
    if char == "$" or char == "#":
     raise ValueError("Unsupported complex Steam launch options")
    if char in "\"'":
     quote = char
     index += 1
     continue
    if char == "\\":
     if index + 1 >= length or value[index + 1] in "\r\n":
      raise ValueError("Unsupported complex Steam launch options")
     cooked.append(value[index + 1])
     index += 2
     continue
    cooked.append(char)
    index += 1
    continue
   if quote == "\"" and char == "\\":
    if index + 1 >= length or value[index + 1] in "\r\n":
     raise ValueError("Unsupported complex Steam launch options")
    cooked.append(value[index + 1])
    index += 2
    continue
   if char == quote:
    quote = None
    index += 1
    continue
   if char == "`" or char == "$":
    raise ValueError("Unsupported complex Steam launch options")
   cooked.append(char)
   index += 1
  if quote is not None:
   raise ValueError("Unsupported complex Steam launch options")
  if index == start:
   raise ValueError("Unsupported complex Steam launch options")
  words.append((start, index, "".join(cooked)))
 return words


def _merge_overrides(value: str) -> str:
 parts = value.split(";") if value else []
 merged = []
 seen_d3d9 = False
 for part in parts:
  if not part or "=" not in part:
   raise ValueError("Unsupported WINEDLLOVERRIDES value")
  name, setting = part.split("=", 1)
  if (not name or not setting or
      any(not (char.isalnum() or char in "_.*?-") for char in name) or
      any(not (char.isalnum() or char in "_,.*?-") for char in setting)):
   raise ValueError("Unsupported WINEDLLOVERRIDES value")
  if name.casefold() == "d3d9":
   if not seen_d3d9:
    merged.append("d3d9=n,b")
    seen_d3d9 = True
  else:
   merged.append(part)
 if not seen_d3d9:
  merged.append("d3d9=n,b")
 return ";".join(merged)


def merge_launch_options(options: str) -> str:
 """Add the native d3d9 override without evaluating any launch-option text."""
 if not isinstance(options, str):
  raise ValueError("Steam LaunchOptions is not a string")
 if not options.strip():
  return _TARGET
 words = _shell_words(options)
 commands = [word for word in words if word[2].casefold() == "%command%"]
 if len(commands) > 1:
  raise ValueError("Unsupported complex Steam launch options")
 assignments = [word for word in words if word[2].startswith("WINEDLLOVERRIDES=")]
 if len(assignments) > 1:
  raise ValueError("Duplicate WINEDLLOVERRIDES launch setting")
 if assignments:
  start, end, cooked = assignments[0]
  if commands and start > commands[0][0]:
   raise ValueError("Unsupported complex Steam launch options")
  if not commands and start != words[0][0]:
   raise ValueError("Unsupported complex Steam launch options")
  if "=" not in cooked:
   raise ValueError("Unsupported WINEDLLOVERRIDES value")
  merged = 'WINEDLLOVERRIDES="{}"'.format(_merge_overrides(cooked.split("=", 1)[1]))
  options = options[:start] + merged + options[end:]
  offset = len(merged) - (end - start)
  if commands and commands[0][0] > start:
   commands = [(commands[0][0] + offset, commands[0][1] + offset, commands[0][2])]
 if commands:
  return options if assignments else 'WINEDLLOVERRIDES="d3d9=n,b" ' + options
 if assignments:
  # Keep a leading assignment as an environment prefix and insert Steam's
  # command token after it; the remaining text is treated as native arguments.
  _, end, _ = assignments[0]
  # Re-find the edited token's end, because its quote style may have changed.
  edited = _shell_words(options)[0]
  return options[:edited[1]] + " %command%" + options[edited[1]:]
 return 'WINEDLLOVERRIDES="d3d9=n,b" %command% ' + options.strip()


def _steam_id32(value: str) -> str:
 if not value.isdecimal():
  raise ValueError("Steam user must be a numeric account ID or SteamID64")
 number = int(value)
 if number >= STEAM_ID64_BASE:
  number -= STEAM_ID64_BASE
 if number < 0:
  raise ValueError("Invalid Steam user ID")
 return str(number)


def _profiles(root: Path) -> List[Tuple[str, Path]]:
 userdata = root / "userdata"
 try:
  return [(entry.name, entry) for entry in userdata.iterdir()
          if entry.is_dir() and entry.name.isdecimal() and
          (entry / "config" / "localconfig.vdf").is_file()]
 except OSError:
  return []


def _selected_profile(root: Path, steam_user: Optional[str]) -> Path:
 profiles = dict(_profiles(root))
 if steam_user is not None:
  account = _steam_id32(str(steam_user))
  if account not in profiles:
   raise ValueError("Selected --steam-user has no localconfig.vdf")
  return profiles[account]
 login = root / "config" / "loginusers.vdf"
 try:
  _, text, _ = _read_vdf(login)
  users = _unique(steam._parse_vdf_document(text), "users")
  active = []
  for account in _branch(users, "users"):
   if not account.key.isdecimal() or not isinstance(account.value, list):
    continue
   recent = _unique(account.value, "MostRecent")
   if recent is not None and recent.value == "1":
    active.append(_steam_id32(account.key))
  if len(active) == 1 and active[0] in profiles:
   return profiles[active[0]]
  if len(active) > 1:
   raise ValueError("Multiple active Steam accounts; choose one with --steam-user")
 except (OSError, UnicodeError, steam._VdfError, ValueError) as exc:
  if isinstance(exc, ValueError) and "Multiple active" in str(exc):
   raise
 if len(profiles) == 1:
  return next(iter(profiles.values()))
 if len(profiles) > 1:
  raise ValueError("Multiple Steam profiles; choose one with --steam-user")
 raise ValueError("No Steam profile localconfig.vdf found")


def steam_running(steam_root: Path, proc_root: Path = Path("/proc")) -> bool:
 """Check only process executables below this Steam root; never read environments."""
 if os.name != "posix" or not proc_root.is_dir():
  return False
 try:
  root = steam_root.resolve()
  for process in proc_root.iterdir():
   if not process.name.isdecimal():
    continue
   try:
    executable = (process / "exe").resolve(strict=True)
    executable.relative_to(root)
    return True
   except (OSError, ValueError):
    continue
 except OSError:
  return False
 return False


def _metadata(record: dict) -> Tuple[Path, Path]:
 required = {"version", "gameAppId", "steamRoot", "profilePath", "existed",
             "before", "expected", "createdText"}
 if not isinstance(record, dict) or set(record) != required or record["version"] != 1:
  raise ValueError("Invalid Steam launch-options backup")
 if record["gameAppId"] != APP_ID or not isinstance(record["steamRoot"], str):
  raise ValueError("Invalid Steam launch-options backup")
 if (not isinstance(record["profilePath"], str) or not isinstance(record["existed"], bool) or
     not isinstance(record["expected"], str) or not isinstance(record["createdText"], str) or
     (record["before"] is not None and not isinstance(record["before"], str))):
  raise ValueError("Invalid Steam launch-options backup")
 if record["existed"] == (record["before"] is None):
  raise ValueError("Invalid Steam launch-options backup")
 root = Path(record["steamRoot"])
 profile = Path(record["profilePath"])
 if not profile.name.isdecimal() or profile.parent != root / "userdata":
  raise ValueError("Invalid Steam launch-options backup")
 return root, profile


def _load_record(path: Path) -> Optional[dict]:
 if not path.exists():
  return None
 try:
  record = json.loads(path.read_text(encoding="utf-8"))
 except (OSError, json.JSONDecodeError) as exc:
  raise ValueError("Invalid Steam launch-options backup") from exc
 _metadata(record)
 return record


def prepare(installation: steam.Installation, backup: Path, steam_user: Optional[str] = None):
 """Prepare a CAS-protected, reversible selected-profile LaunchOptions edit."""
 root = Path(installation.steam_root).resolve()
 profile = _selected_profile(root, steam_user)
 config = profile / "config" / "localconfig.vdf"
 raw, text, bom = _read_vdf(config)
 document = steam._parse_vdf_document(text)
 steam_branch = _option_entry(text, document)
 apps = _unique(_branch(steam_branch, "Steam"), "apps")
 app = _unique(_branch(apps, "apps"), APP_ID) if apps is not None else None
 option = _unique(_branch(app, APP_ID), "LaunchOptions") if app is not None else None
 current = option.value if option is not None else ""
 if option is not None and not isinstance(current, str):
  raise ValueError("Steam LaunchOptions is not a string")
 expected = merge_launch_options(current)
 changed_text, existed, before, created = _replace_or_insert(text, expected)
 data = _encode_vdf(changed_text, bom)
 record_path = Path(backup) / RECORD
 prior = _load_record(record_path)
 metadata = prior or {"version": 1, "gameAppId": APP_ID, "steamRoot": str(root),
                       "profilePath": str(profile), "existed": existed, "before": before,
                       "expected": expected, "createdText": created}
 if prior is not None:
  saved_root, saved_profile = _metadata(prior)
  if saved_root != root or saved_profile != profile:
   raise ValueError("Existing Steam launch-options backup belongs to another profile")
  if current == prior["expected"]:
   metadata = dict(prior)
   metadata["expected"] = expected
  else:
   # Ownership changed: preserve the user's latest value as the new baseline.
   # A retry after a failed settings write likewise captures its actual value.
   metadata = {"version": 1, "gameAppId": APP_ID, "steamRoot": str(root),
               "profilePath": str(profile), "existed": existed, "before": before,
               "expected": expected, "createdText": created}
 if data != raw and steam_running(root):
  raise ValueError("Close Steam before changing its launch options")
 return {"config": config, "raw": raw, "data": data, "record": record_path,
         "metadata": metadata, "steam_root": root}


def apply(plan) -> None:
 """Write the prior value record before a CAS-checked localconfig update."""
 if plan["config"].read_bytes() != plan["raw"]:
  raise ValueError("Steam launch options changed during installation; retry with Steam closed")
 if plan["data"] == plan["raw"]:
  return
 if steam_running(plan["steam_root"]):
  raise ValueError("Steam started during installation; close it and retry")
 plan["record"].parent.mkdir(parents=True, exist_ok=True)
 write(plan["record"], json.dumps(plan["metadata"], ensure_ascii=False, indent=2).encode("utf-8"))
 if plan["config"].read_bytes() != plan["raw"]:
  raise ValueError("Steam config changed while saving ownership; recovery record retained")
 write(plan["config"], plan["data"])


def restore(backup: Path) -> None:
 """Restore only the exact launch option this adapter last wrote."""
 record_path = Path(backup) / RECORD
 record = _load_record(record_path)
 if record is None:
  return
 root, profile = _metadata(record)
 config = profile / "config" / "localconfig.vdf"
 raw, text, bom = _read_vdf(config)
 document = steam._parse_vdf_document(text)
 steam_branch = _option_entry(text, document)
 apps = _unique(_branch(steam_branch, "Steam"), "apps")
 app = _unique(_branch(apps, "apps"), APP_ID) if apps is not None else None
 option = _unique(_branch(app, APP_ID), "LaunchOptions") if app is not None else None
 if option is None or option.value != record["expected"]:
  # The user changed or removed this setting after installation.  Relinquish
  # ownership so a later install captures that user choice as its new baseline.
  record_path.unlink()
  return
 if steam_running(root):
  raise ValueError("Close Steam before restoring its launch options")
 if record["existed"]:
  updated = text[:option.value_span[0]] + _quote(record["before"]) + text[option.value_span[1]:]
 else:
  updated = _remove_entry(text, option)
 if config.read_bytes() != raw:
  raise ValueError("Steam config changed during restoration; recovery record retained")
 write(config, _encode_vdf(updated, bom))
 record_path.unlink()
