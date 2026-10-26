"""Validate public assets, relative documentation links and export boundaries."""
from pathlib import Path
from urllib.parse import unquote, urlsplit
from PIL import Image
import csv,hashlib,json,re,sys,zipfile
root=Path(__file__).resolve().parents[1]
errors=[]
# Inspect versioned files in CI and the export files before its first commit.
try:
 import subprocess
 paths=[root/p for p in subprocess.check_output(['git','-c','safe.directory='+root.as_posix(),'ls-files','-z'],cwd=root).decode().split('\0') if p]
 if not paths:raise ValueError()
except (subprocess.CalledProcessError,ValueError):
 paths=[p for p in root.rglob('*') if p.is_file() and not any(x in {'.git','.pio','.build','managed_components','__pycache__'} for x in p.relative_to(root).parts) and p.name not in {'secrets.h','LocalConfig.h','platformio_local.ini','BoothAssets.h','HardwareTestAssets.h','sdkconfig.gladiator_c6'}]
for p in paths:
 rel=p.relative_to(root).as_posix()
 if p.stat().st_size>=50*1024*1024:errors.append('Oversized Git file: '+rel)
 if p.name in {'secrets.h','LocalConfig.h','platformio_local.ini'} or p.suffix.lower() in {'.bin','.exe','.gcode','.3mf','.pem','.key','.zip'}:errors.append('Private or generated file: '+rel)
 if p.suffix.lower() in {'.jpg','.jpeg'}:
  with Image.open(p) as im:
   if im.getexif() or im.info.get('xmp') or im.info.get('comment'):errors.append('Camera metadata retained: '+rel)
   if max(im.size)>1800:errors.append('Unoptimized photo: '+rel)
 if p.suffix.lower()=='.md':
  text=p.read_text(encoding='utf-8')
  text=re.sub(r'```.*?```','',text,flags=re.S)
  for target in re.findall(r'\]\(([^)]+)\)',text):
   target=target.strip('<>').split(' "')[0]
   if urlsplit(target).scheme or target.startswith('#'):continue
   dest=unquote(target.split('#')[0])
   if not (p.parent/dest).exists():errors.append('Broken link: '+rel+' -> '+target)
 if p.suffix.lower() in {'.h','.cpp','.py','.js','.cjs','.ini','.json','.md','.html','.csv','.txt','.yml'}:
  text=p.read_text(encoding='utf-8')
  for pattern in [r'gh[pousr]_[A-Za-z0-9]{30,}',r'github_pat_[A-Za-z0-9_]{30,}',r'AKIA[A-Z0-9]{16}',r'-----BEGIN (?:RSA |EC |OPENSSH )?PRIVATE KEY-----']:
   if re.search(pattern,text):errors.append('Possible credential: '+rel)
manifest=json.loads((root/'photos/manifest.json').read_text())
for item in manifest:
 p=root/item['path']
 if not p.exists() or hashlib.sha256(p.read_bytes()).hexdigest()!=item['sha256']:errors.append('Photo manifest mismatch: '+item['path'])
source=json.loads((root/'SOURCE_MANIFEST.json').read_text())
for item in source['files']:
 p=root/item['path']
 if not p.exists() or hashlib.sha256(p.read_bytes()).hexdigest()!=item['sha256']:errors.append('Source manifest mismatch: '+item['path'])
for row in list(csv.reader((root/'hardware/BOM.csv').open())):
 if len(row)!=5:errors.append('Invalid BOM column count')
if errors:
 print('\n'.join(sorted(set(errors))));sys.exit(1)
print(f'Export valid: {len(paths)} files, {len(manifest)} photos, local links, metadata and source hashes checked.')
