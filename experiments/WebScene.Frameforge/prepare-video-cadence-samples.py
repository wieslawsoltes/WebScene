#!/usr/bin/env python3
"""Prepare reproducible manual video-cadence fixtures (requires ffmpeg/ffprobe).

Run with --output PATH, then copy the resulting MP4s to the demo's Assets/assets.
Use media-cadence-demo.html as media-demo.html in the existing AOT media host.
Original movie frames are preserved; only BBB audio is converted from MP3 to AAC.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import urllib.request
import zipfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--output', type=Path, required=True)
args = parser.parse_args()
args.output.mkdir(parents=True, exist_ok=True)
urls = {
    'sintel-trailer-24fps.mp4': 'https://download.blender.org/durian/trailer/sintel_trailer-1080p.mp4',
    'bbb-60fps-original.zip': 'https://download.blender.org/demo/movies/BBB/bbb_sunflower_1080p_60fps_normal.mp4.zip',
    'tears-of-steel-original.mp4': 'https://test.playready.microsoft.com/media/profficialsite/tearsofsteel_4k_60s_24fps.12000kbps.3840x2160.h265-8b.2ch.128kbps.aac.mp4',
}
for name, url in urls.items():
    target = args.output / name
    if not target.exists():
        partial = target.with_suffix(target.suffix + '.partial')
        urllib.request.urlretrieve(url, partial)
        partial.replace(target)
original = args.output / 'bbb_sunflower_1080p_60fps_normal.mp4'
if not original.exists():
    with zipfile.ZipFile(args.output / 'bbb-60fps-original.zip') as archive:
        # Extract one known member, not arbitrary archive paths.
        with archive.open(original.name) as source, original.open('wb') as target:
            import shutil
            shutil.copyfileobj(source, target)
for output, source, options in [
    ('big-buck-bunny-60fps.mp4', original, ['-t', '60', '-c:v', 'copy', '-c:a', 'aac', '-b:a', '192k']),
    ('tears-of-steel.mp4', args.output / 'tears-of-steel-original.mp4', ['-c', 'copy', '-tag:v', 'hvc1']),
]:
    target = args.output / output
    if not target.exists():
        subprocess.run(['ffmpeg', '-v', 'error', '-i', str(source), '-map', '0:v:0', '-map', '0:a:0',
                        *options, '-movflags', '+faststart', str(target)], check=True)
manifest = {'sources': urls, 'attribution': 'Blender Foundation: Big Buck Bunny, Sintel, Tears of Steel', 'files': {}}
for name in ['big-buck-bunny-60fps.mp4', 'sintel-trailer-24fps.mp4', 'tears-of-steel.mp4']:
    path = args.output / name
    info = json.loads(subprocess.check_output(['ffprobe', '-v', 'error', '-show_streams', '-show_format', '-of', 'json', str(path)]))
    digest = hashlib.sha256()
    with path.open('rb') as data:
        for chunk in iter(lambda: data.read(1024 * 1024), b''):
            digest.update(chunk)
    manifest['files'][name] = {'sha256': digest.hexdigest(), 'probe': info}
(args.output / 'cadence-samples.json').write_text(json.dumps(manifest, indent=2) + '\n')
print(args.output.resolve())
