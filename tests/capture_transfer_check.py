"""Exercise actual incremental capture code with controlled UI completion."""
from pathlib import Path
import os,subprocess,tempfile
root=Path(__file__).resolve().parents[1];sdk=Path(os.environ.get('IDF_PATH',root.parent/'esp-idf'))
source=(root/'main/ui.cpp').read_text()
with tempfile.TemporaryDirectory(prefix='scout-capture-') as t:
    out=Path(t);(out/'capture.inc').write_text(source[source.index('static char* capture_request_id;'):source.index('static SemaphoreHandle_t native_tile_mutex;')])
    (out/'pcm_config.h').write_text('#define MBEDTLS_BASE64_C\n')
    json=sdk/'components/json/cJSON';mbed=sdk/'components/mbedtls/mbedtls'
    flags=['-O2','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer','-Wno-deprecated-declarations',
           '-DMBEDTLS_CONFIG_FILE="pcm_config.h"','-I',str(out),'-I',str(json),'-I',str(mbed/'include'),'-I',str(mbed/'library')]
    objects=[]
    for src in [json/'cJSON.c',mbed/'library/base64.c',mbed/'library/constant_time.c']:
        target=out/(src.stem+'.o');objects.append(str(target));subprocess.run(['cc',*flags,'-c',str(src),'-o',str(target)],check=True)
    subprocess.run(['c++','-std=c++17',*flags,str(root/'tests/capture_transfer_check.cpp'),*objects,'-o',str(out/'check')],check=True)
    subprocess.run([str(out/'check')],check=True)
