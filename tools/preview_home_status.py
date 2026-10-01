"""Render production home chrome at native size and enlarged state comparisons."""
import ast, subprocess, sys
from pathlib import Path
from array import array
from PIL import Image,ImageDraw
from storage_hygiene import scratch_directory
ROOT=Path(__file__).resolve().parents[1]
OUT=Path(sys.argv[1]) if len(sys.argv)>1 else ROOT/'test-results/status-spacing-0.11.27'
OUT.mkdir(parents=True,exist_ok=True)
tree=ast.parse((ROOT/'tests/ui_layout_check.py').read_text())
prefix=next(ast.literal_eval(n.value) for n in tree.body if isinstance(n,ast.Assign) and any(isinstance(t,ast.Name) and t.id=='prefix' for t in n.targets))
s=(ROOT/'main/ui.cpp').read_text();body=s[s.index('static constexpr uint16_t INK'):s.index('static uint64_t clock_us')]
suffix=r'''
int main(int argc,char**argv){
 std::vector<uint16_t> frame(410*502);canvas=frame.data();rows=502;top=0;ui_alpha=255;
 State s;s.screen=Screen::Home;present_epoch=1790180400;
 const int pct[]={100,64,12,78,100,80,-1,-1,0};
 const Battery::Mode modes[]={Battery::Mode::Full,Battery::Mode::Discharging,Battery::Mode::Discharging,Battery::Mode::Charging,Battery::Mode::Charging,Battery::Mode::Paused,Battery::Mode::Unknown,Battery::Mode::Absent,Battery::Mode::Discharging};
 for(int i=0;i<45;i++){
  s.battery_percent=i<9?pct[i]:78;s.usb_connected=i==0||i==3||i==4||i==5||i==7||i>=9;s.bluetooth_connected=i==1;
  s.battery_mode=i<9?modes[i]:Battery::Mode::Charging;s.battery_external=s.usb_connected;s.battery_charging=s.battery_mode==Battery::Mode::Charging;
  present_now_us=(i<9?18:i-9)*83333LL;
  std::fill(frame.begin(),frame.end(),0);header(s);footer(s);
  char path[512];snprintf(path,sizeof(path),"%s/%d.rgb565",argv[1],i);FILE*f=fopen(path,"wb");fwrite(frame.data(),2,frame.size(),f);fclose(f);
 }
}
'''
with scratch_directory('status-preview',reserve_mib=32) as tmp:
 tmp=Path(tmp);(tmp/'preview.cpp').write_text(prefix+body+suffix)
 subprocess.run(['c++','-std=c++17','-O2','-I',str(ROOT/'main'),str(tmp/'preview.cpp'),'-o',str(tmp/'preview')],check=True)
 subprocess.run([str(tmp/'preview'),str(tmp)],check=True)
 sheet=Image.new('RGB',(3*400,3*220),'#141419');draw=ImageDraw.Draw(sheet)
 labels=['USB / fully charged','Bluetooth / 64%','Offline / low','Charging / 78%','Charging / 100%','Connected / charge paused','Status unavailable','Battery absent','Empty battery']
 frames=[]
 for i in range(45):
  a=array('H',(tmp/f'{i}.rgb565').read_bytes())
  im=Image.frombytes('RGB',(410,502),bytes(c for v in a for c in ((v>>11)*255//31,((v>>5)&63)*255//63,(v&31)*255//31)))
  if i<9:
   im.save(OUT/f'chrome-{i}.png')
   x=i%3*400;y=i//3*220;draw.text((x+22,y+16),labels[i],fill='#ddd8d4')
   sheet.paste(im.crop((180,28,230,64)).resize((100,72),Image.Resampling.NEAREST),(x+150,y+45))
   sheet.paste(im.crop((166,464,242,500)).resize((228,108),Image.Resampling.NEAREST),(x+86,y+106))
  else:frames.append(im.crop((166,464,242,500)).resize((456,216),Image.Resampling.NEAREST))
 sheet.save(OUT/'chrome-states.png')
 frames[0].save(OUT/'charging.png',save_all=True,append_images=frames[1:],duration=83.333,loop=0)
print(OUT/'chrome-states.png')
