"""Compare production native chrome before/after per-worker clock caching."""
from pathlib import Path
import ast,subprocess,os
from storage_hygiene import scratch_directory
ROOT=Path(__file__).resolve().parents[1]
parsed=ast.parse((ROOT/'tests/ui_layout_check.py').read_text())
prefix=next(ast.literal_eval(n.value) for n in parsed.body if isinstance(n,ast.Assign) and any(isinstance(t,ast.Name) and t.id=='prefix' for t in n.targets))
previous=(ROOT/'test-results/listening-aurora-0.11.31/static/ui-before-clock-cache.cpp').read_text()
current=(ROOT/'main/ui.cpp').read_text()
suffix=r'''
int main(){
 std::vector<uint16_t> frame(410*502);State s;s.screen=Screen::Home;canvas=frame.data();top=0;rows=502;
 uint64_t hash=1469598103934665603ull;unsigned cases=0;
 for(auto tz:{"UTC0","PST8PDT","KST-9"}){
  setenv("TZ",tz,1);tzset();s.revision++;
  for(int j=0;j<102;j++){
   present_epoch=j<2?j*100:1790221800+(j-2)*60*17;
   for(int repeat=0;repeat<2;repeat++)for(int alpha:{89,190,255}){
    ui_alpha=alpha;s.usb_connected=cases%2;for(size_t i=0;i<frame.size();i++)frame[i]=uint16_t(i*97+cases*11);
    header(s);footer(s);for(auto p:frame){hash^=p;hash*=1099511628211ull;}cases++;
   }
  }
  // Same epoch, changed timezone: revision invalidates both formatted strings.
  setenv("TZ","GMT-4",1);tzset();s.revision++;header(s);for(auto p:frame){hash^=p;hash*=1099511628211ull;}cases++;
 }
 printf("%u rendered cases, hash %016llx\n",cases,(unsigned long long)hash);
}
'''
with scratch_directory('clock-cache-check',reserve_mib=30) as tmp:
 p=Path(tmp);results=[]
 for label,text in [('before',previous),('after',current)]:
  body=text[text.index('static constexpr uint16_t INK'):text.index('static uint64_t clock_us')]
  source=p/(label+'.cpp');source.write_text(prefix+body+suffix);exe=p/label
  subprocess.run(['c++','-std=c++17','-O2','-fsanitize=address,undefined','-I'+str(ROOT/'main'),str(source),'-o',str(exe)],check=True)
  results.append(subprocess.check_output([str(exe)],text=True,env={**os.environ,'ASAN_OPTIONS':'detect_leaks=0'}))
 assert results[0]==results[1],results
 print(results[0].strip());print('Native chrome is pixel-identical across repeats, minute/day changes, invalid time, timezone revision changes, and partial opacity.')
