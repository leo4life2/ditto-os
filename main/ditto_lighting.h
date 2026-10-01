#pragma once
#include "backends/soft/a3d_soft_backend.h"
#include <algorithm>
#include <cmath>

namespace DittoLighting {
// The body sits against black, with opaque native UI and face meshes above it.
// Four percent transmission is composed before RGB565 quantization so the
// subtle body translucency adds no per-pixel work or repeated alpha stacking.
inline constexpr float BodyOpacity=.96f;
// A directional studio-light response evaluated once at startup. The live
// renderer reconstructs its normal/light coordinate across each triangle.
// Purple pigment stays in the diffuse layer; the clear wet coat reflects white.
inline void build(a3d::SoftBackend::SurfaceMap& map,float r,float g,float b) {
    a3d::SoftBackend::ColorRamp ramp;
    a3d::SoftBackend::makeColorRamp(ramp,r,g,b);map.base=ramp.base;
    const float thresholds[4]={.125f,.625f,.875f,.375f};
    for(int shade=0;shade<512;shade++) {
        float ndoth=shade/511.f;
        float diffuse=.38f+.52f*ndoth;
        float specular=.12f*std::pow(ndoth,12.f)+.88f*std::pow(ndoth,80.f);
        float light=diffuse+.96f*specular;
        float pigment=std::min(light,.94f);
        float white=std::clamp((light-.90f)/.84f,0.f,1.f);
        white=white*white*(3-2*white);
        float color[3]={r*pigment*(1-white)+white,g*pigment*(1-white)+white,b*pigment*(1-white)+white};
        for(int fade=0;fade<=256;fade++)for(int phase=0;phase<4;phase++) {
            float f=fade/256.f*BodyOpacity,q=thresholds[phase];
            unsigned red=std::min(31u,unsigned(color[0]*f*31+q));
            unsigned green=std::min(63u,unsigned(color[1]*f*63+q));
            unsigned blue=std::min(31u,unsigned(color[2]*f*31+q));
            map.colors[fade*a3d::SoftBackend::SurfaceMap::RowStride+shade*4+phase]=(red<<11)|(green<<5)|blue;
        }
    }
}
}
