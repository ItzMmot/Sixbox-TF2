#!/usr/bin/env python3
"""Replace release assets sequentially, recovering duplicate/starter uploads."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import time
from urllib.parse import quote


class UploadError(RuntimeError):
    pass


def gh(*args):
    result = subprocess.run(['gh', *args], capture_output=True, text=True,
                            encoding='utf-8', errors='replace')
    if result.returncode:
        raise UploadError(result.stderr.strip() or result.stdout.strip() or 'gh failed')
    return result.stdout


def assets(repo, release_id):
    pages = json.loads(gh('api', '--paginate', '--slurp',
                         f'repos/{repo}/releases/{release_id}/assets?per_page=100'))
    return [asset for page in pages for asset in page]


def matches(asset, path, digest):
    return (asset.get('state') == 'uploaded' and asset.get('size') == path.stat().st_size
            and asset.get('digest') == digest)


def retryable(error):
    text = str(error).lower()
    return (('422' in text and ('already exists' in text or 'already_exists' in text))
            or any(word in text for word in ('http 429', 'http 500', 'http 502',
                   'http 503', 'http 504', 'timeout', 'timed out', 'connection reset',
                   'unexpected eof', 'verification failed')))


def upload(repo, tag, release_id, path, attempts=4, sleep=time.sleep):
    checksum = hashlib.sha256()
    with path.open('rb') as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b''):
            checksum.update(chunk)
    digest = 'sha256:' + checksum.hexdigest()
    for attempt in range(attempts):
        try:
            existing = [a for a in assets(repo, release_id) if a['name'] == path.name]
            # A lost upload response need not force another large transfer.
            if len(existing) == 1 and matches(existing[0], path, digest):
                print(f'Verified existing asset: {path.name}', flush=True)
                return
            for asset in existing:
                try:
                    gh('api', '--method', 'DELETE', f"repos/{repo}/releases/assets/{asset['id']}")
                except UploadError as error:
                    if 'HTTP 404' not in str(error):
                        raise
            gh('release', 'upload', tag, str(path.resolve()), '--repo', repo)
            uploaded = [a for a in assets(repo, release_id) if a['name'] == path.name]
            if (len(uploaded) != 1 or uploaded[0].get('state') != 'uploaded'
                    or uploaded[0].get('size') != path.stat().st_size
                    or (uploaded[0].get('digest') and uploaded[0]['digest'] != digest)):
                raise UploadError(f'Verification failed for {path.name}')
            print(f'Uploaded and verified: {path.name}', flush=True)
            return
        except UploadError as error:
            if attempt + 1 == attempts or not retryable(error):
                raise UploadError(f'{path.name}: {error}') from error
            delay = 2 ** (attempt + 1)
            print(f'{path.name}: retrying after {delay}s: {error}', flush=True)
            sleep(delay)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--repo', default=os.environ.get('GH_REPO'))
    parser.add_argument('--tag', required=True)
    parser.add_argument('--directory', type=Path, required=True)
    args = parser.parse_args()
    if not args.repo:
        parser.error('--repo or GH_REPO is required')
    paths = sorted(p for p in args.directory.iterdir() if p.is_file())
    if not paths:
        parser.error('Release bundle is empty')
    release = json.loads(gh('api', f'repos/{args.repo}/releases/tags/{quote(args.tag, safe="")}'))
    for path in paths:
        upload(args.repo, args.tag, release['id'], path)


if __name__ == '__main__':
    main()
