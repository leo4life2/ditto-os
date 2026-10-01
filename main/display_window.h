#pragma once

// CO5300 updates use paired columns and rows, as in the Waveshare BSP
// rounder callback. End coordinates are exclusive, including each DMA strip.
struct DisplayWindow {
  int x, y, width, height;
};
inline DisplayWindow display_window(int x, int y, int width, int height) {
  int left = x & ~1, upper = y & ~1;
  return {left, upper, ((x + width + 1) & ~1) - left,
          ((y + height + 1) & ~1) - upper};
}
inline int display_strip_rows(int capacity, int width) {
  return (capacity / width) & ~1;
}

// Coalesce adjacent strips only when padding adds at most 8% more pixels.
// Pixels come from one immutable completed frame. This removes short tail
// DMA transactions without changing the scan synchronization or the image.
inline int display_coalesce(DisplayWindow* areas,int count) {
  int out=0,source_area=0;
  for(int i=0;i<count;i++) {
    const auto next=areas[i];
    if(out) {
      auto& prev=areas[out-1];
      const int left=prev.x<next.x?prev.x:next.x;
      const int right=prev.x+prev.width>next.x+next.width?prev.x+prev.width:next.x+next.width;
      const int sum=source_area+next.width*next.height;
      if(prev.y+prev.height==next.y && (right-left)*(prev.height+next.height)*100<=sum*108) {
        prev={left,prev.y,right-left,prev.height+next.height};source_area=sum;continue;
      }
    }
    areas[out++]=next;source_area=next.width*next.height;
  }
  return out;
}
