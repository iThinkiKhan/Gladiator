"""Create local build configuration, without overwriting existing settings."""
from pathlib import Path
import shutil
root=Path(__file__).resolve().parents[1]
for name in ['shared/secrets','cam/src/LocalConfig']:
    source=root/(name+'.example.h'); target=root/(name+'.h')
    if not target.exists(): shutil.copyfile(source,target); print('Created '+str(target.relative_to(root)))
source=root/'platformio_local.ini.example';target=root/'platformio_local.ini'
if not target.exists(): shutil.copyfile(source,target); print('Created platformio_local.ini')
