#!/usr/bin/env python3
"""Install checked display-mode edits; never modify unknown executables."""
import argparse
import hashlib
import json
import os
import struct
import tempfile
from pathlib import Path

ORIGINAL = '4a27c727160d2565f02488883e86a34ac6e0f650aee528f0a1e24017eedc5e9e'
LABEL = 0x567559
# These are file offsets, verified against the stock build's disassembly.
MODE_WIDTH = 0x209982
MODE_HEIGHT = 0x20998c
INTERNAL_EDGE = 0x209996
TEXTURE_WIDTH_LIMIT = 0x31b81e
TEXTURE_HEIGHT_LIMIT = 0x31b832
CORE_FILES = ('game.exe', 'gurumin.exe', 'd3d9.dll')
INI = 'GuruminModern.ini'
NOTICE = 'GuruminModern-FXAA-LICENSE.txt'


def sha(b):
 return hashlib.sha256(b).hexdigest()


def write(p, b):
 with tempfile.NamedTemporaryFile(dir=p.parent, delete=False) as f:
  f.write(b)
  f.flush()
  os.fsync(f.fileno())
  tmp = Path(f.name)
 os.chmod(tmp, p.stat().st_mode if p.exists() else 0o644)
 os.replace(tmp, p)


def modified(original, w, h):
 b = bytearray(original)
 for off, value in [(MODE_WIDTH, w), (MODE_HEIGHT, h), (INTERNAL_EDGE, max(w, h)),
                    (TEXTURE_WIDTH_LIMIT, max(2048, w, h)),
                    (TEXTURE_HEIGHT_LIMIT, max(2048, w, h))]:
  struct.pack_into('<I', b, off, value)
 b[LABEL:LABEL + 13] = f"{w:4d}  x  {h:4d}".encode()
 return bytes(b)


def load_manifest(manifest):
 try:
  data = json.loads(manifest.read_text())
  files = data['files']
  for name in CORE_FILES:
   entry = files[name]
   if not isinstance(entry['existed'], bool) or not isinstance(entry['installed_sha256'], str):
    raise ValueError(f'{name} has invalid fields')
  for name in (INI, NOTICE):
   if name in files:
    entry = files[name]
    if not isinstance(entry['existed'], bool) or not isinstance(entry['installed_sha256'], str):
     raise ValueError(f'{name} has invalid fields')
  return data
 except (OSError, ValueError, KeyError, TypeError, json.JSONDecodeError) as exc:
  raise SystemExit(f'Manifest error: {exc}')


def verify_backups(backup, manifest, names):
 """Read and verify every original before any restore write is attempted."""
 originals = {}
 for name in names:
  entry = manifest['files'][name]
  if not entry['existed']:
   continue
  try:
   original = (backup / name).read_bytes()
  except OSError as exc:
   raise SystemExit(f'{name}: backup error: {exc}')
  expected = entry.get('backup_sha256')
  # Old manifests did not retain a backup hash. Their executable backups must
  # still be this installer's known stock build; old installs never back up DLLs.
  if expected is None and name in ('game.exe', 'gurumin.exe'):
   expected = ORIGINAL
  if not isinstance(expected, str) or sha(original) != expected:
   raise SystemExit(f'{name}: backup hash mismatch; refusing restore')
  originals[name] = original
 return originals


def installed_files_match(root, manifest, names=None, originals=None):
 for name in names or managed_files(manifest):
  entry = manifest['files'][name]
  dest = root / name
  current = dest.read_bytes() if dest.exists() else None
  if originals is not None:
   if current is None and not entry['existed']:
    continue
   if entry['existed'] and current == originals.get(name):
    continue
  if current is None or sha(current) != entry['installed_sha256']:
   if name in ('d3d9.dll', NOTICE):
    bundled = Path(__file__).resolve().parents[1] / 'dist' / name
    if bundled.exists() and current == bundled.read_bytes():
     continue  # Exact release payload copied over an older managed install.
   if current is not None and sha(current) == entry.get('previous_sha256'):
    continue
   raise SystemExit(f'{name} changed since installation; refusing to overwrite')


def managed_files(manifest):
 return CORE_FILES + tuple(name for name in (INI, NOTICE) if name in manifest['files'])


def status(root):
 backup = root / 'GuruminModern-backup'
 manifest_path = backup / 'manifest.json'
 manifest = None
 if manifest_path.exists():
  try:
   manifest = load_manifest(manifest_path)
   print('manifest: ok')
  except SystemExit as exc:
   print(f'manifest: error: {exc}')
 else:
  print('manifest: missing')
 names = CORE_FILES + tuple(name for name in (INI, NOTICE)
     if (root / name).exists() or (manifest and name in manifest['files']))
 for name in names:
  path = root / name
  if not path.exists():
   print(f'{name}: missing')
   continue
  try:
   data = path.read_bytes()
   message = f'{name}: {sha(data)}'
   if name in ('game.exe', 'gurumin.exe'):
    if len(data) < INTERNAL_EDGE + 4:
     raise ValueError('file is too short for display offsets')
    message += ' output {}x{} internal edge {}'.format(
     struct.unpack_from('<I', data, MODE_WIDTH)[0],
     struct.unpack_from('<I', data, MODE_HEIGHT)[0],
     struct.unpack_from('<I', data, INTERNAL_EDGE)[0])
   if manifest and name not in manifest['files']:
    message += ' unowned'
   elif manifest and sha(data) != manifest['files'][name]['installed_sha256']:
    message += ' edited configuration (preserved)' if name == INI else ' error: differs from manifest'
   print(message)
  except (OSError, ValueError, struct.error) as exc:
   print(f'{name}: error: {exc}')


def restore(root):
 backup = root / 'GuruminModern-backup'
 manifest_path = backup / 'manifest.json'
 if not manifest_path.exists():
  raise SystemExit('No installation manifest; refusing restore')
 manifest = load_manifest(manifest_path)
 core_names = CORE_FILES + ((NOTICE,) if NOTICE in manifest['files'] else ())
 restore_names = list(core_names)
 config_preserved = False
 if INI in manifest['files']:
  entry = manifest['files'][INI]
  config = root / INI
  if config.exists() and sha(config.read_bytes()) != entry['installed_sha256']:
   config_preserved = True
  else:
   restore_names.append(INI)
 originals = verify_backups(backup, manifest, restore_names)
 # Steam validation or a previous partial restore can already have put an
 # owned file back. Accept only its exact installed or verified original data;
 # unknown later mods still block restore. This also makes retry idempotent.
 installed_files_match(root, manifest, core_names, originals)
 for name in restore_names:
  entry = manifest['files'][name]
  dest = root / name
  if entry['existed']:
   write(dest, originals[name])
  elif dest.exists():
   dest.unlink()
 manifest['state'] = 'restored'
 write(manifest_path, json.dumps(manifest, indent=2).encode())
 print('Restored pre-install executables and DLL. Backups retained.')
 if config_preserved:
  print(f'{INI} was edited after installation; preserved in place.')


def install(root, width, height, writer=write):
 if not (640 <= width <= 7680 and 480 <= height <= 4320):
  raise SystemExit('Resolution outside supported test bounds')
 backup = root / 'GuruminModern-backup'
 manifest_path = backup / 'manifest.json'
 prior_manifest = manifest_path.read_bytes() if manifest_path.exists() else None
 manifest = load_manifest(manifest_path) if prior_manifest is not None else None
 candidates = {}
 before = {}
 for name in ('game.exe', 'gurumin.exe'):
  try:
   current = (root / name).read_bytes()
  except OSError as exc:
   raise SystemExit(f'{name}: cannot read executable: {exc}')
  before[name] = current
  original_path = root / (name + '.guruminfix-original')
  backup_path = backup / name
  stock = current if sha(current) == ORIGINAL else b''
  if not stock and original_path.exists():
   stock = original_path.read_bytes()
  if not stock and manifest and backup_path.exists():
   stock = backup_path.read_bytes()
  if sha(stock) != ORIGINAL:
   raise SystemExit(f'{name}: unknown stock executable')
  known = sha(current) == ORIGINAL
  if manifest:
   known |= sha(current) == manifest['files'][name]['installed_sha256']
   known |= sha(current) == manifest['files'][name].get('previous_sha256')
  # The previous resolution-only patch is recognized exactly, not by a few bytes.
  previous = bytearray(stock)
  struct.pack_into('<I', previous, MODE_WIDTH, 3440)
  struct.pack_into('<I', previous, MODE_HEIGHT, 1440)
  previous[LABEL:LABEL + 13] = b'3440  x  1440'
  known |= current == previous
  if not known:
   raise SystemExit(f'{name}: unknown modifications; refusing install')
  candidates[name] = modified(stock, width, height)
 try:
  candidates['d3d9.dll'] = (Path(__file__).resolve().parents[1] / 'dist/d3d9.dll').read_bytes()
 except OSError as exc:
  raise SystemExit(f'd3d9.dll: cannot read installer DLL: {exc}')
 template = Path(__file__).resolve().parents[1] / 'dist' / INI
 if template.exists():
  try:
   candidates[INI] = template.read_bytes().replace(b'Width=0', f'Width={width}'.encode()).replace(b'Height=0', f'Height={height}'.encode())
  except OSError as exc:
   raise SystemExit(f'{INI}: cannot read installer template: {exc}')
 notice = Path(__file__).resolve().parents[1] / 'dist' / NOTICE
 if notice.exists():
  candidates[NOTICE] = notice.read_bytes()
 names = tuple(candidates)
 for name in names:
  path = root / name
  before[name] = path.read_bytes() if path.exists() else None
 if manifest:
  known_names = CORE_FILES + ((NOTICE,) if NOTICE in manifest['files'] else ())
  installed_files_match(root, manifest, known_names, verify_backups(backup, manifest, known_names))
  if NOTICE in candidates and NOTICE not in manifest['files']:
   if before[NOTICE] is not None:
    raise SystemExit(f'{NOTICE}: unowned file already exists; refusing install')
   manifest['files'][NOTICE] = {'existed': False}
  if INI in candidates and INI not in manifest['files']:
   if before[INI] is not None:
    raise SystemExit(f'{INI}: unowned file already exists; refusing install')
   manifest['files'][INI] = {'existed': False}
  elif INI in candidates and before[INI] is not None:
   # Owned configuration, including adopted manual preferences, stays intact while
   # normal executable/DLL updates proceed.
   candidates.pop(INI)
   before.pop(INI)
 else:
  # The optional installer may be run after the release DLL was copied into
  # the game folder. Adopt only the exact bundled DLL, never an unknown proxy.
  extracted = before['d3d9.dll'] == candidates['d3d9.dll']
  if before['d3d9.dll'] is not None and not extracted:
   raise SystemExit('A D3D9 DLL already exists; resolve that conflict first')
  if NOTICE in before and before[NOTICE] is not None and not (extracted and before[NOTICE] == candidates[NOTICE]):
   raise SystemExit(f'{NOTICE}: unowned file already exists; refusing install')
  if INI in before and before[INI] is not None:
   if not extracted:
    raise SystemExit(f'{INI}: unowned file already exists; refusing install')
   candidates[INI] = before[INI]  # Preserve settings saved after a manual install.
  manifest = {'files': {}}
  for name in names:
   existed = before[name] is not None and not (extracted and name in ('d3d9.dll', NOTICE))
   entry = {'existed': existed}
   if existed:
    entry['backup_sha256'] = sha(before[name])
   manifest['files'][name] = entry
  try:
   backup.mkdir(exist_ok=True)
   for name in names:
    if manifest['files'][name]['existed']:
     write(backup / name, before[name])
  except OSError as exc:
   raise SystemExit(f'Backup error: {exc}')
 for name, data in candidates.items():
  manifest['files'][name]['installed_sha256'] = sha(data)
  manifest['files'][name]['previous_sha256'] = sha(before[name]) if before[name] is not None else None
 manifest['resolution'] = [width, height]
 manifest['state'] = 'installing'
 recovery_manifest = json.dumps(manifest, indent=2).encode()
 try:
  # Persist ownership before touching any game file. A killed process can be
  # recovered using this same manifest and the verified original backups.
  writer(manifest_path, recovery_manifest)
  for name, data in candidates.items():
   writer(root / name, data)
  manifest['state'] = 'installed'
  # Keep the last verified owned hashes too: an interrupted final commit plus
  # incomplete rollback may leave either revision. No arbitrary file is trusted.
  writer(manifest_path, json.dumps(manifest, indent=2).encode())
 except BaseException as exc:
  failures = []
  for name, data in before.items():
   dest = root / name
   try:
    current = dest.read_bytes() if dest.exists() else None
    if current == data:
     continue
    if current != candidates[name]:
     raise OSError('file changed by another writer; preserving it')
    if data is None:
     dest.unlink()
    else:
     write(dest, data)
   except (OSError, ValueError) as rollback_error:
    failures.append(f'{name}: {rollback_error}')
  if not failures:
   try:
    if prior_manifest is None:
     if manifest_path.exists():
      manifest_path.unlink()
    else:
     write(manifest_path, prior_manifest)
   except OSError as rollback_error:
    failures.append(f'manifest: {rollback_error}')
  if failures:
   try:
    write(manifest_path, recovery_manifest)
   except OSError:
    pass # The last persisted manifest still owns both known revisions.
   raise RuntimeError('Rollback incomplete; recovery manifest and backups retained. Close other writers and rerun install or restore. ' + '; '.join(failures)) from exc
  raise
 print(f'Installed {width}x{height} display defaults, internal edge {max(width, height)}. Resolution, frame cap and camera settings are in the launcher Graphics / Audio tab.')


def main(argv=None):
 parser = argparse.ArgumentParser()
 parser.add_argument('action', choices=['install', 'status', 'restore'])
 parser.add_argument('--game', type=Path, required=True)
 parser.add_argument('--width', type=int, default=3440)
 parser.add_argument('--height', type=int, default=1440)
 args = parser.parse_args(argv)
 if args.action == 'status':
  status(args.game)
 elif args.action == 'restore':
  restore(args.game)
 else:
  install(args.game, args.width, args.height)


if __name__ == '__main__':
 main()
