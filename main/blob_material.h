#pragma once
#include <algorithm>
#include <cmath>
#include "blob_motion.h"

namespace Blob {
// Symmetric log strain (xx, yy, zz, xy, xz, yz). Off-diagonal terms are
// essential: a diagonal load must compress along that diagonal, not X and Y
// independently. Memory and velocity stay in body space when gravity turns.
struct Material {
  float strain[6]={},velocity[6]={},memory[6]={};
  float matrix[9]={1,0,0,0,1,0,0,0,1},normal_matrix[9]={1,0,0,0,1,0,0,0,1};
  float scale[3]={1,1,1}; // Diagonal projections; determinant is volume().
  BlobMotion::V support{},gravity{};
  static void deviator(float *v) {
    float mean=(v[0]+v[1]+v[2])/3;
    for(int k=0;k<3;k++)v[k]-=mean;
  }
  void impulse(float x,float y,float z) {
    float stress[]={x,y,z};deviator(stress);
    for(int k=0;k<3;k++)velocity[k]+=stress[k];
  }
  void step(float dt,float x,float y,float z,BlobMotion::V g={}) {
    gravity=BlobMotion::finite(g)?BlobMotion::cap(g,1.5f):BlobMotion::V{};
    float stress[6]={x,y,z,0,0,0};
    const float weight=gravity.length();
    BlobMotion::V target{};
    if(weight>.00001f) {
      const float v[]={gravity.x/weight,gravity.y/weight,gravity.z/weight};
      const float load=-1.5f*weight;
      for(int k=0;k<3;k++)stress[k]+=load*v[k]*v[k];
      stress[3]=load*v[0]*v[1];stress[4]=load*v[0]*v[2];stress[5]=load*v[1]*v[2];
      // Approximate the support point of the blob's rounded envelope in the
      // physical down direction. No fixed screen-bottom floor remains.
      const float radii2[]={1.f,.88f*.88f,.60f*.60f};
      const float d=std::sqrt(radii2[0]*v[0]*v[0]+radii2[1]*v[1]*v[1]+radii2[2]*v[2]*v[2]);
      target={radii2[0]*v[0]/d,radii2[1]*v[1]/d,radii2[2]*v[2]/d};
    }
    deviator(stress);
    int steps=std::max(1,int(std::ceil(dt*120)));float h=dt/steps;
    const float relax=1-std::exp(-h/.35f),follow=1-std::exp(-h/.18f);
    for(int i=0;i<steps;i++) {
      support=support+(target-support)*follow;
      for(int k=0;k<6;k++) {
        float force=stress[k]-4.5f*strain[k]-4.f*(strain[k]-memory[k])-2.f*velocity[k];
        velocity[k]+=force*h;strain[k]+=velocity[k]*h;
        memory[k]+=(strain[k]-memory[k])*relax;
      }
      deviator(strain);deviator(velocity);deviator(memory);
      // Row-sum bound also bounds every eigenvalue, including diagonal loads.
      const float xy=std::abs(strain[3]),xz=std::abs(strain[4]),yz=std::abs(strain[5]);
      float peak=std::max({std::abs(strain[0])+xy+xz,std::abs(strain[1])+xy+yz,std::abs(strain[2])+xz+yz});
      if(peak>.48f) {
        float factor=.48f/peak;
        for(int k=0;k<6;k++){strain[k]*=factor;velocity[k]*=.5f;memory[k]*=factor;}
      }
    }
    updateTransform();
  }
  void updateTransform() {
    // exp(S) is positive definite and det(exp(S))=exp(trace(S))=1. A small
    // Jacobi eigensolve runs once per physics frame, never per vertex.
    float a[3][3]={{strain[0],strain[3],strain[4]},
                   {strain[3],strain[1],strain[5]},
                   {strain[4],strain[5],strain[2]}};
    float q[3][3]={{1,0,0},{0,1,0},{0,0,1}};
    for(int sweep=0;sweep<5;sweep++)for(int p=0;p<2;p++)for(int r=p+1;r<3;r++) {
      const float off=a[p][r];if(std::abs(off)<1e-8f)continue;
      const float tau=(a[r][r]-a[p][p])/(2*off);
      const float t=std::copysign(1.f,tau)/(std::abs(tau)+std::sqrt(1+tau*tau));
      const float c=1/std::sqrt(1+t*t),s=t*c;
      a[p][p]-=t*off;a[r][r]+=t*off;a[p][r]=a[r][p]=0;
      for(int k=0;k<3;k++) {
        if(k!=p && k!=r){float ap=a[k][p],ar=a[k][r];a[k][p]=a[p][k]=c*ap-s*ar;a[k][r]=a[r][k]=s*ap+c*ar;}
        float qp=q[k][p],qr=q[k][r];q[k][p]=c*qp-s*qr;q[k][r]=s*qp+c*qr;
      }
    }
    float e[3];for(int k=0;k<3;k++)e[k]=std::exp(a[k][k]);
    for(int i=0;i<3;i++)for(int j=0;j<3;j++) {
      float f=0,n=0;for(int k=0;k<3;k++){float w=q[i][k]*q[j][k];f+=w*e[k];n+=w/e[k];}
      matrix[i*3+j]=f;normal_matrix[i*3+j]=n;
    }
    for(int k=0;k<3;k++)scale[k]=matrix[k*3+k];
  }
  float volume() const {
    const auto &m=matrix;
    return m[0]*(m[4]*m[8]-m[5]*m[7])-m[1]*(m[3]*m[8]-m[5]*m[6])+m[2]*(m[3]*m[7]-m[4]*m[6]);
  }
  float activity() const {
    float sum=0;for(int k=0;k<6;k++)sum+=std::abs(velocity[k])+std::abs(strain[k]-memory[k]);
    return sum;
  }
  void deform(float *p,float *n) const {
    const float anchor[]={support.x,support.y,support.z};
    float v[3],out[3],normal[3];for(int k=0;k<3;k++)v[k]=p[k]-anchor[k];
    for(int i=0;i<3;i++) {
      out[i]=anchor[i];normal[i]=0;
      for(int j=0;j<3;j++){out[i]+=matrix[i*3+j]*v[j];normal[i]+=normal_matrix[i*3+j]*n[j];}
    }
    for(int k=0;k<3;k++){p[k]=out[k];n[k]=normal[k];}
  }
};
}
