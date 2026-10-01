"""Compare a zero-influence skinning shortcut against the original equations."""
from pathlib import Path
import subprocess,sys,os
from storage_hygiene import scratch_directory
ROOT=Path(__file__).resolve().parents[1]
previous=(ROOT/'test-results/listening-aurora-0.11.31/fast/ditto_rig.h').read_text()
old=previous.replace('namespace DittoRig {','namespace ReferenceRig {')
new=(ROOT/'main/ditto_rig.h').read_text()
assert 'if(weights.w[k]==0' in new and 'if(weights.w[k]==0' not in previous
check=r'''
#include "old.h"
#include "new.h"
#include <cassert>
#include <cstring>
#include <cstdio>
int main(){
 unsigned cases=0;
 for(int clip=0;clip<Avatar::ClipCount;clip++){
  ReferenceRig::Skeleton a;DittoRig::Skeleton b;
  for(int frame=0;frame<150;frame++){
   a.step(1.f/30,clip,frame/30.f,frame<120);b.step(1.f/30,clip,frame/30.f,frame<120);
   for(int y=-12;y<=12;y++)for(int x=-13;x<=13;x++)for(int z=-1;z<=1;z++){
    float p[]={x*.095f,y*.085f,z*.32f},n[]={.31f,-.53f,.83f},ao[3],an[3],bo[3],bn[3];
    float ad=a.deform(p,n,ao,an),bd=b.deform(p,n,bo,bn);
    for(int k=0;k<3;k++){assert(ao[k]==bo[k]);assert(an[k]==bn[k]);}assert(ad==bd);cases++;
   }
  }
 }
 printf("%u poses/points: position, Jacobian determinant and surface normal exactly equal.\n",cases);
}
'''
with scratch_directory('rig-zero-influence',reserve_mib=20) as tmp:
 p=Path(tmp);(p/'old.h').write_text(old);(p/'new.h').write_text(new);(p/'test.cpp').write_text(check)
 subprocess.run(['c++','-std=c++17','-O2','-fsanitize=address,undefined','-I'+str(ROOT/'main'),str(p/'test.cpp'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True,env={**os.environ,'ASAN_OPTIONS':'detect_leaks=0'})
