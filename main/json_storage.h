#pragma once
#include "esp_heap_caps.h"
#include <cstddef>

// Frequent small audio/control messages stay in fast internal memory. Bulk
// diagnostics and output formatting use PSRAM, so their temporary JSON trees
// cannot consume the reserve needed by input, audio and rendering.
namespace JsonStorage {
inline thread_local bool external=false;
inline void *allocate(size_t bytes) {
  if(!external) {
    if(void *p=heap_caps_malloc(bytes,MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT))return p;
  }
  // Match the original malloc path's external fallback for small commands if
  // networking temporarily exhausts the fast heap.
  return heap_caps_malloc(bytes,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
}
class External {
  bool previous;
 public:
  External():previous(external){external=true;}
  ~External(){external=previous;}
  External(const External&)=delete;
  External& operator=(const External&)=delete;
};
}
