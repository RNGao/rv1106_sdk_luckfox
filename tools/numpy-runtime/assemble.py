#!/usr/bin/env python3
"""Assemble an isolated ARMv7 runtime from official Alpine APKs; no install scripts."""
import base64
import hashlib
import io
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tarfile
import zlib

BASE = Path(__file__).resolve().parents[2] / 'output/numpy-runtime'
CACHE = BASE / 'downloads'
ROOT = BASE / 'python-numpy'
URL = 'https://dl-cdn.alpinelinux.org/alpine/v3.23'


def download(url, path):
    if not path.exists():
        subprocess.run(['curl', '-fsSL', '--retry', '3', '--connect-timeout', '20',
                        '--max-time', '180', url, '-o', str(path) + '.part'], check=True)
        Path(str(path) + '.part').rename(path)


def main():
    CACHE.mkdir(parents=True, exist_ok=True)
    ROOT.mkdir(parents=True, exist_ok=True)
    packages, providers = {}, {}
    for repo in ('main', 'community'):
        path = CACHE / f'{repo}-APKINDEX.tar.gz'
        download(f'{URL}/{repo}/armv7/APKINDEX.tar.gz', path)
        with tarfile.open(path) as archive:
            index = archive.extractfile('APKINDEX').read().decode()
        for block in index.strip().split('\n\n'):
            p = dict(line.split(':', 1) for line in block.splitlines() if ':' in line)
            p['repo'] = repo
            packages[p['P']] = p
            for provide in p.get('p', '').split():
                providers.setdefault(re.split(r'[=<>~]', provide)[0], []).append(p['P'])
    selected = {}

    def resolve(dep):
        if dep.startswith('!'):
            raise RuntimeError(f'Unsupported conflicting dependency: {dep}')
        name = re.split(r'[=<>~]', dep)[0]
        if name not in packages:
            choices = providers.get(name, [])
            if len(choices) != 1:
                raise RuntimeError(f'Ambiguous/missing dependency {dep}: {choices}')
            name = choices[0]
        if name in selected:
            return
        p = selected[name] = packages[name]
        for child in p.get('D', '').split():
            resolve(child)

    resolve('python3')
    resolve('py3-numpy')
    manifest = []
    for name, p in sorted(selected.items()):
        filename = f"{name}-{p['V']}.apk"
        url = f"{URL}/{p['repo']}/armv7/{filename}"
        path = CACHE / filename
        print(f"Fetching {filename}", flush=True)
        download(url, path)
        raw = path.read_bytes()
        if len(raw) != int(p['S']):
            raise RuntimeError(f'Incorrect size: {filename}')
        members, rest = [], raw
        while rest:
            dec = zlib.decompressobj(31)
            unpacked = dec.decompress(rest)
            consumed = len(rest) - len(dec.unused_data)
            if not dec.eof or consumed == 0:
                raise RuntimeError(f'Invalid APK gzip member: {filename}')
            members.append((rest[:consumed], unpacked))
            rest = dec.unused_data
        control_checksum = 'Q1' + base64.b64encode(
            hashlib.sha1(members[-2][0]).digest()).decode()
        if control_checksum != p['C']:
            raise RuntimeError(f'APK control checksum differs from index: {filename}')
        info = None
        for _, unpacked in members:
            with tarfile.open(fileobj=io.BytesIO(unpacked)) as archive:
                if '.PKGINFO' in archive.getnames():
                    info = archive.extractfile('.PKGINFO').read().decode()
        expected = re.search(r'^datahash = (\w+)$', info or '', re.M)
        if not expected or hashlib.sha256(members[-1][0]).hexdigest() != expected[1]:
            raise RuntimeError(f'APK data checksum failed: {filename}')
        with tarfile.open(fileobj=io.BytesIO(members[-1][1])) as archive:
            for entry in archive:
                rel = Path(entry.name)
                if rel.is_absolute() or '..' in rel.parts:
                    raise RuntimeError(f'Unsafe APK path: {entry.name}')
                dest = ROOT / rel
                if entry.isdir():
                    dest.mkdir(parents=True, exist_ok=True)
                elif entry.isfile():
                    dest.parent.mkdir(parents=True, exist_ok=True)
                    if dest.is_symlink():
                        raise RuntimeError(f'File collides with symlink: {dest}')
                    dest.write_bytes(archive.extractfile(entry).read())
                    dest.chmod(entry.mode & 0o777)
                elif entry.issym():
                    dest.parent.mkdir(parents=True, exist_ok=True)
                    target = entry.linkname
                    if target.startswith('/'):
                        target = os.path.relpath(ROOT / target.lstrip('/'), dest.parent)
                    resolved = (dest.parent / target).resolve()
                    if not resolved.is_relative_to(ROOT.resolve()):
                        raise RuntimeError(f'Unsafe symlink: {entry.name}')
                    if dest.is_symlink():
                        dest.unlink()
                    dest.symlink_to(target)
                else:
                    raise RuntimeError(f'Unsupported APK entry: {entry.name}')
        manifest.append({'name': name, 'version': p['V'], 'architecture': p['A'],
                         'url': url, 'sha256': hashlib.sha256(raw).hexdigest(),
                         'dependencies': p.get('D', ''), 'license': p.get('L', '')})
    (ROOT / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    source = Path(__file__).resolve().parent
    (ROOT / 'bin').mkdir(exist_ok=True)
    shutil.copyfile(source / 'python3', ROOT / 'bin/python3')
    (ROOT / 'bin/python3').chmod(0o755)
    shutil.copyfile(source / 'validate.py', ROOT / 'validate.py')
    shutil.copyfile(source / 'validate_processes.py', ROOT / 'validate_processes.py')
    shutil.copyfile(source / 'sitecustomize.py', ROOT / 'usr/lib/python3.12/sitecustomize.py')
    with tarfile.open(BASE / 'python-numpy-armv7.tar.gz', 'w:gz') as archive:
        archive.add(ROOT, arcname='python-numpy')
    print(f'Assembled {len(manifest)} packages in {ROOT}', flush=True)


if __name__ == '__main__':
    main()
