#include "display_window.h"
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <random>
#include <vector>
int main(){
 std::mt19937 random(94273);
 uint64_t before=0,after=0,extra=0;
 for(int trial=0;trial<20000;trial++){
  std::vector<DisplayWindow> a;int y=0;
  while(y<502){int h=std::min(2+2*int(random()%9),502-y);int x=2*(random()%100);int w=2+2*(random()%((410-x)/2));a.push_back({x,y,w,h});y+=h+(random()%4==0?2:0);}
  auto original=a;int count=display_coalesce(a.data(),a.size());assert(count<=int(original.size()));a.resize(count);
  uint64_t orig_area=0,new_area=0;std::vector<bool> covered(410*502),reference(410*502);
  for(auto w:original){orig_area+=w.width*w.height;int step=display_strip_rows(4920,w.width);before+=(w.height+step-1)/step;for(int r=w.y;r<w.y+w.height;r++)for(int x=w.x;x<w.x+w.width;x++)reference[r*410+x]=true;}
  for(auto w:a){assert(!((w.x|w.y|w.width|w.height)&1));assert(w.x>=0&&w.y>=0&&w.x+w.width<=410&&w.y+w.height<=502);new_area+=w.width*w.height;int step=display_strip_rows(4920,w.width);after+=(w.height+step-1)/step;for(int r=w.y;r<w.y+w.height;r++)for(int x=w.x;x<w.x+w.width;x++)covered[r*410+x]=true;}
  assert(new_area*100<=orig_area*108);
  for(size_t p=0;p<reference.size();p++)assert(!reference[p]||covered[p]);
  extra+=new_area-orig_area;
 }
 DisplayWindow gap[]={{0,0,20,18},{0,20,20,18}};assert(display_coalesce(gap,2)==2);
 DisplayWindow pair[]={{4,4,300,18},{4,22,300,18}};assert(display_coalesce(pair,2)==1&&pair[0].height==36);
 printf("20,000 windows sets: paired in bounds, all changed pixels covered, <=8%% padding; strips %llu -> %llu, added pixels %llu\n",before,after,extra);
}
