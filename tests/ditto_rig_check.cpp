#include "ditto_rig.h"
#include <cassert>
#include <cstdio>
int main() {
  using namespace DittoRig;
  Skeleton idle;
  float minimum=100,max_error=0;
  for(int clip=0;clip<Avatar::ClipCount;clip++) {
    Skeleton rig;
    for(int frame=0;frame<150;frame++) {
      rig.step(1.f/30,clip,frame/30.f,frame<120);
      // Interior normal probes; render_rig_emote checks every actual skin vertex.
      for(int x=-8;x<=8;x+=2)for(int y=-6;y<=6;y+=2) {
        float p[]={x*.1f,y*.1f,.35f},n[]={0,0,1},q[3],normal[3];
        auto w=bind(p);float sum=0;
        for(float a:w.w){assert(a>=0&&a<=1);sum+=a;}assert(sum<=1.00001f);
        float det=rig.deform(p,n,q,normal,w);minimum=std::min(minimum,det);
        if(!std::isfinite(det)||det<=.05f){fprintf(stderr,"near-singular blend clip=%d frame=%d p=(%f,%f,%f) det=%f\n",clip,frame,p[0],p[1],p[2],det);return 1;}
        for(int axis=0;axis<2;axis++) {
          float plus[]={p[0],p[1],p[2]},minus[]={p[0],p[1],p[2]},qp[3],qm[3],unused[3];
          plus[axis]+=.0002f;minus[axis]-=.0002f;
          rig.deform(plus,n,qp,unused);rig.deform(minus,n,qm,unused);
          float dot=0;for(int k=0;k<3;k++)dot+=(qp[k]-qm[k])/.0004f*normal[k];
          max_error=std::max(max_error,std::abs(dot));assert(std::abs(dot)<.004f);
        }
      }
    }
    assert(!rig.active); // Every canceled pose returns exactly to zero.
  }
  for(int i=0;i<300;i++)idle.step(1.f/30,Avatar::Idle,i/30.f,true);
  assert(!idle.active);
  // A rigid parent carries every child. Rest-pose offsets remain fixed.
  Skeleton parent;parent.active=true;Control c;c.rz=.3f;c.y=.1f;
  auto world=transform(c,joints[Body].pivot);
  for(auto& a:parent.palette)a=world;
  float p[]={.3f,.4f,.5f},n[]={0,0,1},out[3],normal[3],expected[3];
  parent.deform(p,n,out,normal);world.point(p,expected);
  for(int k=0;k<3;k++)assert(std::abs(out[k]-expected[k])<1e-6f);
  printf("16 clips: positive Jacobians (min %.4f), analytic-normal error %.6f, normalized weights, parent inheritance and exact canceled/idle rest passed.\n",minimum,max_error);
}
