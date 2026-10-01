#include "blob_render.h"
#include <cassert>
#include <cstdio>
int main(){
 BlobRenderer ordinary,listening;
 Blob::Touch touch;
 BlobMotion::Sample m;m.valid=true;m.gravity={0,-1,0};
 for(int frame=0;frame<240;frame++){
  if(frame==30){touch={true,240,260,240,260,1};ordinary.physics.setImpact(1,true,.3f,.15f);listening.physics.setImpact(1,true,.3f,.15f);}
  if(frame>=40&&frame<70){touch.x++;touch.y--;}
  if(frame==150)touch.down=false;
  ordinary.setEmote(Avatar::Idle,frame/30.f);
  listening.setEmote(frame>=38&&frame<150?Avatar::Listening:Avatar::Idle,frame/30.f);
  ordinary.advance(1.f/30,touch,false,m,&m.gravity);
  listening.advance(1.f/30,touch,false,m,&m.gravity);
  for(int x=-8;x<=8;x+=2)for(int y=-6;y<=6;y+=2){
   float p[]={x*.1f,y*.1f,.35f},n[]={0,0,1},a[3],b[3],na[3],nb[3];
   ordinary.physics.deform(p,n,a,na);listening.physics.deform(p,n,b,nb);
   for(int i=0;i<3;i++){assert(a[i]==b[i]);assert(na[i]==nb[i]);}
  }
  if(frame>=65&&frame<150){assert(!ordinary.rig.active);assert(listening.rig.active);assert(listening.physics.stretch()>.01f);}
 }
 assert(listening.listening_amount==0);assert(!listening.rig.active);
 puts("240 hold/pull/release frames: listening skeleton stays active under stretch; physical deformation and normals match exactly; release settles to zero.");
}
