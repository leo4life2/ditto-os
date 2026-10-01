#include "backends/soft/a3d_soft_backend.h"
#include <cassert>
#include <cmath>
#include <cstdio>
int main(){
 a3d::SoftBackend before,after;
 for(auto* r:{&before,&after}){r->setViewportSize(410,502);r->setPerspective(45,410.f/502,.05,20);float eye[]={.4f,.2f,4},target[]={0,0,0},up[]={0,1,0};r->setLookAt(eye,target,up);}
 after.shiftProjection(0,60);
 float a[16],b[16];before.currentMVP(a);after.currentMVP(b);
 int cases=0;float error=0;
 for(int z=-8;z<8;z++)for(int y=-20;y<=20;y++)for(int x=-10;x<=10;x++){
  float pa[4]={},pb[4]={};float p[]={x*.1f,y*.1f,z*.1f,1};
  for(int r=0;r<4;r++)for(int c=0;c<4;c++){pa[r]+=a[c*4+r]*p[c];pb[r]+=b[c*4+r]*p[c];}
  assert(pa[0]==pb[0]&&pa[2]==pb[2]&&pa[3]==pb[3]);
  float shift=(pb[1]/pb[3]-pa[1]/pa[3])*251;error=std::max(error,std::abs(shift-60));assert(std::abs(shift-60)<.00015);cases++;
 }
 printf("%d projected points preserve horizontal/depth coordinates and shift 60 px, max error %.8f px.\n",cases,error);
}
