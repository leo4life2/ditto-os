#pragma once
// Shared actual-device viewport/fade configuration for renderer equivalence QA.
#ifdef SCOUT_QUALITY_NATIVE
static uint16_t quality_fade[502];
static void qualityTarget(a3d::Viewer& v,a3d::ViewerConfig& c) {
#ifndef SCOUT_QUALITY_TILE
#define SCOUT_QUALITY_TILE 18
#endif
    c.tileHeight=SCOUT_QUALITY_TILE;c.visibleRowBegin=54;c.visibleRowEnd=382;
#if defined(SCOUT_QUALITY_OPTIMIZED) && !defined(SCOUT_INTERNAL_COLOR)
    v.setColorTarget([](void*,int y,int){return pixels.data()+y*410;},nullptr);
#endif
}
static void qualityLighting(a3d::Viewer& v,int workers) {
    for(int y=0;y<502;y++) {
        int gy=y+42,light=256;
        if(gy<166)light=std::clamp((gy-96)*256/70,0,256);
        if(gy>290){float f=std::clamp((gy-290)/134.f,0.f,1.f);float e=f*f*f*(f*(f*6-15)+10);light=std::min(light,int(256*(1-e)+.5f));}
        quality_fade[y]=light;
    }
#ifdef SCOUT_QUALITY_OPTIMIZED
    static a3d::SoftBackend::SurfaceVertex cache[2][512];
    static a3d::SoftBackend::SurfaceSpan spans[2][SCOUT_QUALITY_TILE];
#endif
    for(int w=0;w<workers;w++) {
        v.backend(w).setSmoothRowFade(quality_fade);
#ifdef SCOUT_QUALITY_OPTIMIZED
#ifndef SCOUT_INTERNAL_COLOR
        v.backend(w).setSurfaceVertexCache(cache[w],512);
#endif
        v.backend(w).setSurfaceSpans(spans[w],SCOUT_QUALITY_TILE);
#endif
    }
}
#else
static void qualityTarget(a3d::Viewer&,a3d::ViewerConfig&) {}
static void qualityLighting(a3d::Viewer&,int) {}
#endif
