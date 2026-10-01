"""Compile actual emit functions against cJSON and a bounded fake USB writer."""
from pathlib import Path
import os,subprocess,tempfile
root=Path(__file__).resolve().parents[1]
sdk=Path(os.environ.get('IDF_PATH',root.parent/'esp-idf'))
source=(root/'main/main.cpp').read_text()
emit=source[source.index('static int write_output'):source.index('void event_message')]
with tempfile.TemporaryDirectory(prefix='scout-wire-') as temp:
    out=Path(temp)
    (out/'emit.inc').write_text(emit)
    flags=['-O2','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer']
    json=sdk/'components/json/cJSON'
    subprocess.run(['cc',*flags,'-I',str(json),'-c',str(json/'cJSON.c'),'-o',str(out/'json.o')],check=True)
    subprocess.run(['c++','-std=c++17','-pthread',*flags,'-I',str(out),'-I',str(json),'-I',str(root/'main'),str(root/'tests/wire_output_check.cpp'),str(out/'json.o'),'-o',str(out/'check')],check=True)
    subprocess.run([str(out/'check')],check=True)
