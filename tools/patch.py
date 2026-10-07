#!/usr/bin/env python3
"""Gurumin Enhanced Rendering: one checked installer on Windows and Linux."""
import argparse
import ctypes
import hashlib
import json
import os
import re
import subprocess
import sys
from pathlib import Path

import install
import steam
import steam_launch

PROJECT = 'Gurumin Enhanced Rendering'
ROOT = Path(__file__).resolve().parents[1]


def payload_check():
    manifest = ROOT / 'payload.json'
    if not manifest.exists():
        if not (ROOT / 'dist/d3d9.dll').exists():
            raise ValueError('Build the mod first, or extract the complete release archive')
        return
    for name, expected in json.loads(manifest.read_text())['sha256'].items():
        path = ROOT / name
        if not path.resolve().is_relative_to(ROOT):
            raise ValueError('Invalid payload path')
        if hashlib.sha256(path.read_bytes()).hexdigest() != expected:
            raise ValueError(f'Patch payload damaged: {name}. Extract the release again.')


def game_in_use(root):
    if os.name == 'nt':
        # Windows refuses write access to a running executable/DLL image.
        for name in install.CORE_FILES:
            path = root / name
            if path.exists():
                try:
                    with path.open('r+b'):
                        pass
                except PermissionError as exc:
                    raise ValueError('Close Gurumin and ensure its folder is writable before installing') from exc
        return False
    proc = Path('/proc')
    if proc.exists():
        for pid in proc.iterdir():
            if not pid.name.isdigit():
                continue
            try:
                cmd = (pid / 'cmdline').read_bytes().split(b'\0')[0].decode(errors='replace')
                if Path(cmd).name.lower() in ('game.exe', 'gurumin.exe') and (pid/'cwd').resolve() == root:
                    return True
            except OSError:
                continue
    return False


def desktop_resolution():
    if os.name == 'nt':
        ctypes.windll.user32.SetProcessDPIAware()
        w, h = ctypes.windll.user32.GetSystemMetrics(0), ctypes.windll.user32.GetSystemMetrics(1)
        if 640 <= w <= 7680 and 480 <= h <= 4320:
            return w, h
    else:
        try:
            result = subprocess.run(['xrandr', '--current'], capture_output=True, text=True, timeout=5)
            matches = re.findall(r'^\S+ connected( primary)?(?:[^\n]*?) (\d+)x(\d+)\+\d+\+\d+', result.stdout, re.M)
            matches.sort(key=lambda row: not bool(row[0]))
            for _, w, h in matches:
                if 640 <= int(w) <= 7680 and 480 <= int(h) <= 4320:
                    return int(w), int(h)
        except (OSError, subprocess.TimeoutExpired):
            pass
    return 1920, 1080


def choose(copies, specified, interactive):
    if specified:
        return steam.resolve_game(specified)
    if len(copies) == 1:
        return copies[0].game
    if copies:
        print('Multiple Steam copies found:')
        for index, entry in enumerate(copies, 1):
            print(f'  {index}: {entry.game}')
        if not interactive:
            raise ValueError('Choose a copy with --game PATH; no files changed')
        answer = input('Choose a number, or paste/drop the game folder (blank cancels): ').strip()
        if answer.isdecimal() and 1 <= int(answer) <= len(copies):
            return copies[int(answer)-1].game
    else:
        print('No installed Steam copy found.')
        if not interactive:
            raise ValueError('Drop the game folder or game.exe onto the patch, or use --game PATH')
        answer = input('Paste/drop the game folder or game.exe here (blank cancels): ').strip()
    if not answer:
        raise ValueError('Cancelled; no files changed')
    return steam.resolve_game(answer)


def main(argv=None):
    parser = argparse.ArgumentParser(description=PROJECT+' 1.0 patch')
    parser.add_argument('path', nargs='?', help='Dropped game folder, game.exe or gurumin.exe')
    parser.add_argument('--game', help='Explicit game directory instead of Steam detection')
    parser.add_argument('--action', choices=['install', 'restore', 'status'], default='install')
    parser.add_argument('--steam-root', action='append', type=Path, help='Steam roots to scan instead of defaults (repeatable)')
    parser.add_argument('--list', action='store_true', help='List Steam installations without changing anything')
    parser.add_argument('--width', type=int)
    parser.add_argument('--height', type=int)
    parser.add_argument('--steam-user', help='Steam account ID when active account discovery is ambiguous')
    parser.add_argument('--no-steam-settings', action='store_true', help='Advanced: configure Steam launch options manually')
    args = parser.parse_args(argv)
    print(PROJECT+' 1.0')
    try:
        if sys.version_info < (3, 9):
            raise ValueError('Python 3.9 or newer is required')
        copies = steam.discover(args.steam_root)
        if args.list:
            for entry in copies:
                print(entry.game)
            return 0
        root = choose(copies, args.game or args.path, sys.stdin.isatty()).resolve()
        print('Game:', root)
        if args.action == 'status':
            install.status(root)
            return 0
        if game_in_use(root):
            raise ValueError('Close Gurumin before changing its installed files')
        backup = root / 'GuruminModern-backup'
        if args.action == 'restore':
            install.restore(root)
            if os.name != 'nt' and not args.no_steam_settings:
                steam_launch.restore(backup)
            return 0
        payload_check()
        prepared = None
        if os.name != 'nt' and not args.no_steam_settings:
            match = next((entry for entry in copies if entry.game.resolve() == root), None)
            if not match:
                raise ValueError('Steam account settings could not be linked to this folder. Use --steam-root PATH, or --no-steam-settings and configure launch options manually.')
            prepared = steam_launch.prepare(match, backup, args.steam_user)
        if (args.width is None) != (args.height is None):
            raise ValueError('Supply both --width and --height')
        if args.width is not None:
            width, height = args.width, args.height
        elif (backup/'manifest.json').exists():
            width, height = install.load_manifest(backup/'manifest.json')['resolution']
        else:
            width, height = desktop_resolution()
        install.install(root, width, height)
        if prepared:
            try:
                steam_launch.apply(prepared)
            except (OSError, ValueError) as exc:
                print('Game files installed, but Steam launch settings could not be saved:', exc, file=sys.stderr)
                print('Set Steam launch options to: WINEDLLOVERRIDES="d3d9=n,b" %command%', file=sys.stderr)
                return 1
        elif os.name != 'nt':
            print('Steam launch settings left unchanged. Configure this launch option before playing:')
            print('WINEDLLOVERRIDES="d3d9=n,b" %command%')
        print('Installation complete. Start Gurumin and select Graphics / Audio to configure the mod.')
        return 0
    except KeyboardInterrupt:
        print('Cancelled. Completed file changes were rolled back; backups remain available.', file=sys.stderr)
        return 130
    except (OSError, ValueError, KeyError, TypeError, RuntimeError, EOFError, SystemExit) as exc:
        print('Patch stopped:', exc, file=sys.stderr)
        return 1


if __name__ == '__main__':
    raise SystemExit(main())
