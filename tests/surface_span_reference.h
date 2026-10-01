#pragma once
#include <algorithm>
#include <cstdint>
#include "esp_attr.h"
namespace SpanReference { struct Shader {
        // Keep the integer-only glossy span separate from triangle setup.
        // This lets Xtensa keep its depth/light recurrences in registers; the
        // generic kernel otherwise spills them among many live float planes.
        template <bool ATTENUATED>
        __attribute__((always_inline)) static inline void _surfaceSpanCore(
            uint16_t* row,uint16_t* zp,const uint16_t* colors,int count,
            int32_t z,int32_t dz,int32_t light,int32_t dl,int32_t ddl,
            unsigned phase,unsigned fade)
            {
            for(int i=0;i<count;i++) {
                const uint16_t depth=uint16_t(std::clamp<int32_t>(z>>12,0,65535));
                if(depth>*zp) {
                    const unsigned shade=unsigned(std::clamp<int32_t>(light>>8,0,511));
                    uint16_t c=colors[(shade<<2)|phase];
                    if constexpr(ATTENUATED) {
                        uint32_t packed=(c|(uint32_t(c)<<16))&0x07e0f81f;
                        packed=((packed*fade)>>5)&0x07e0f81f;
                        c=uint16_t(packed|(packed>>16));
                    }
                    *row=c;*zp=depth;
                }
                row++;zp++;z+=dz;light+=dl;dl+=ddl;phase^=1;
            }
            }

        #if defined(ESP_PLATFORM)
        IRAM_ATTR
        #endif
        __attribute__((noinline)) static void _surfaceSpan(
            uint16_t* row,uint16_t* zp,const uint16_t* colors,int count,
            int32_t z,int32_t dz,int32_t light,int32_t dl,int32_t ddl,
            unsigned phase,unsigned fade) {
            _surfaceSpanCore<false>(row,zp,colors,count,z,dz,light,dl,ddl,phase,fade);
        }
        #if defined(ESP_PLATFORM)
        IRAM_ATTR
        #endif
        __attribute__((noinline)) static void _surfaceSpanAttenuated(
            uint16_t* row,uint16_t* zp,const uint16_t* colors,int count,
            int32_t z,int32_t dz,int32_t light,int32_t dl,int32_t ddl,
            unsigned phase,unsigned fade) {
            _surfaceSpanCore<true>(row,zp,colors,count,z,dz,light,dl,ddl,phase,fade);
        }


};}
