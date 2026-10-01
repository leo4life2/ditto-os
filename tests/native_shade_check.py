"""Verify packed native shading against the prior scalar RGB565 operation."""
from pathlib import Path
import subprocess,tempfile
r=Path(__file__).resolve().parents[1]
s=(r/'main/ui.cpp').read_text();body=s[s.index('static void IRAM_ATTR'):s.index('static void native_band')]
prefix='#define IRAM_ATTR\n#include <cstdint>\n#include <cstring>\n#include <cassert>\n#include <cstdio>\nconstexpr int SCREEN_W=410;\n'
suffix=r'''
int main(){alignas(4) uint16_t input[410],output[410];long count=0;
for(int shade=0;shade<=32;shade++)for(int seed=0;seed<65536;seed+=410){for(int x=0;x<410;x++)input[x]=uint16_t(seed+x);shade_row(output,input,shade);for(int x=0;x<410;x++){uint32_t c=(input[x]|(uint32_t(input[x])<<16))&0x07e0f81f;c=((c*shade)>>5)&0x07e0f81f;assert(output[x]==uint16_t(c|(c>>16)));count++;}}
memset(input,0,sizeof(input));for(int shade=0;shade<=32;shade++){shade_row(output,input,shade);for(auto c:output)assert(c==0);}printf("%ld native pixels across all RGB565 colors and 33 fade levels match scalar reference.\n",count);}
'''
with tempfile.TemporaryDirectory(prefix='scout-native-shade-') as d:
 p=Path(d);(p/'test.cpp').write_text(prefix+body+suffix)
 subprocess.run(['c++','-std=c++17','-O2',str(p/'test.cpp'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
