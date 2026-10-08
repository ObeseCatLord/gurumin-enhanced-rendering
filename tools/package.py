#!/usr/bin/env python3
"""Build one allowlisted, game-file-free Windows + Linux v1.0 patch ZIP."""
import argparse
import hashlib
import json
import shutil
import subprocess
import urllib.request
import zipfile
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
PYTHON_URL='https://www.python.org/ftp/python/3.13.16/python-3.13.16-embed-amd64.zip'
PYTHON_SHA='97dae5274cc54867065e8d5a3226e48c35017ed332a0fdb0e27d5b5821961297'
VERSION=(ROOT/'VERSION').read_text().strip()
BASENAME='Gurumin-Enhanced-Rendering-'+VERSION


def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,default=ROOT/'release')
    args=parser.parse_args();out=args.output.resolve();out.mkdir(parents=True,exist_ok=True)
    folder=out/'staging'/BASENAME
    # This directory contains only this tool's generated packaging files.
    if folder.exists():shutil.rmtree(folder)
    folder.mkdir(parents=True)
    files=['README.md','LICENSE','VERSION','CHANGELOG.md','THIRD_PARTY_NOTICES.md',
           'tools/install.py','tools/steam.py','tools/steam_launch.py','tools/patch.py',
           'dist/d3d9.dll','dist/GuruminModern.ini','dist/GuruminModern-FXAA-LICENSE.txt',
           'vendor/minhook/LICENSE.txt','vendor/fxaa/LICENSE.txt']
    for name in files:
        source=ROOT/name;target=folder/name;target.parent.mkdir(parents=True,exist_ok=True)
        shutil.copy2(source,target)
    runtime_zip=ROOT/'downloads'/Path(PYTHON_URL).name;runtime_zip.parent.mkdir(exist_ok=True)
    if not runtime_zip.exists():
        with urllib.request.urlopen(PYTHON_URL,timeout=60) as response:
            runtime_zip.write_bytes(response.read())
    if sha(runtime_zip)!=PYTHON_SHA:raise SystemExit('Official Python runtime checksum mismatch')
    with zipfile.ZipFile(runtime_zip) as archive:
        for entry in archive.infolist():
            if Path(entry.filename).is_absolute() or '..' in Path(entry.filename).parts:raise SystemExit('Invalid runtime ZIP path')
        archive.extractall(folder/'runtime')
    (folder/'runtime/python313._pth').write_text('python313.zip\n.\n../tools\n',encoding='ascii')
    subprocess.run(['x86_64-w64-mingw32-g++','-std=c++17','-O2','-Wall','-Wextra','-static','-municode',
                    str(ROOT/'packaging/windows_launcher.cpp'),'-o',str(folder/'GuruminEnhancedRendering.exe')],check=True)
    shutil.copy2(ROOT/'packaging/GuruminEnhancedRendering.sh',folder/'GuruminEnhancedRendering.sh')
    (folder/'GuruminEnhancedRendering.sh').chmod(0o755)
    output_zip=out/(BASENAME+'.zip')
    # Keep the game-facing payload at the top level; optional installer support
    # stays together below it and continues using the same checked patcher.
    optional=folder/'Optional installers'
    optional.mkdir()
    for name in ('tools','runtime','dist','vendor',
                 'GuruminEnhancedRendering.exe','GuruminEnhancedRendering.sh'):
        shutil.move(str(folder/name),str(optional/name))
    optional_hashes={str(p.relative_to(optional)):sha(p) for p in sorted(optional.rglob('*')) if p.is_file() and p.name!='payload.json'}
    (optional/'payload.json').write_text(json.dumps({'project':'Gurumin Enhanced Rendering','version':VERSION,'sha256':optional_hashes},indent=2)+'\n')
    for name in ('d3d9.dll','GuruminModern-FXAA-LICENSE.txt'):
        shutil.copy2(optional/'dist'/name,folder/name)
    # The runtime provides defaults and writes preferences from its launcher.
    # Omitting a live INI keeps extraction updates from resetting preferences.
    with zipfile.ZipFile(output_zip,'w',zipfile.ZIP_DEFLATED,compresslevel=9) as archive:
        for path in sorted(folder.rglob('*')):
            if path.is_file():archive.write(path,path.relative_to(folder.parent))
    (out/'SHA256SUMS').write_text(f'{sha(output_zip)}  {output_zip.name}\n')
    print(output_zip);print(out/'SHA256SUMS')

if __name__=='__main__':main()
