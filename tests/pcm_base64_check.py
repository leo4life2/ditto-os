"""Compare the production PCM codec with the actual bundled SDK reference."""
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[1]
mbed=root.parent/'esp-idf/components/mbedtls/mbedtls'
with tempfile.TemporaryDirectory(prefix='scout-pcm-') as temp:
 out=Path(temp);(out/'pcm_config.h').write_text('#define MBEDTLS_BASE64_C\n')
 flags=['-O2','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer',
        '-DMBEDTLS_CONFIG_FILE="pcm_config.h"','-I',str(out),'-I',str(mbed/'include')]
 subprocess.run(['cc',*flags,'-I',str(mbed/'library'),'-c',str(mbed/'library/base64.c'),'-o',str(out/'reference.o')],check=True)
 subprocess.run(['cc',*flags,'-I',str(mbed/'library'),'-c',str(mbed/'library/constant_time.c'),'-o',str(out/'constant-time.o')],check=True)
 subprocess.run(['c++','-std=c++17',*flags,'-I',str(root/'main'),str(root/'tests/pcm_base64_check.cpp'),str(out/'reference.o'),str(out/'constant-time.o'),'-o',str(out/'check')],check=True)
 subprocess.run([str(out/'check')],check=True)
