from pathlib import Path
import gzip,hashlib
try: Import('env')
except NameError: env=None
root=Path(env.subst('$PROJECT_DIR')).parent if env is not None else Path(__file__).resolve().parents[2]
lines=['#pragma once','// Generated from tools/booth/index.html and app.js.','namespace booth {']
for symbol,name in [('HTML','index.html'),('JS','app.js'),('SETTINGS','mast-settings.js'),('WORKBENCH','mast-workbench.js')]:
 data=(root/'tools/booth'/name).read_bytes();packed=gzip.compress(data,mtime=0)
 lines.append('static const unsigned char '+symbol+'[]={'+','.join(map(str,packed))+'};')
lines.append('}')
output=root/'gateway/src/BoothAssets.h';text='\n'.join(lines)+'\n'
if not output.exists() or output.read_text()!=text:output.write_text(text,encoding='utf-8')
print('Embedded booth controls')
