"""Actual-device visual/action review. Synthetic credentials only; never joins a network."""
from pathlib import Path
import json,sys,time,os
ROOT=Path(__file__).resolve().parents[1];VERSION=os.environ.get('SCOUT_EXPECTED_VERSION',(ROOT/'VERSION').read_text().strip())
OUT=ROOT/'test-results'/os.environ.get('SCOUT_EVIDENCE_NAME','ui-review-'+VERSION);OUT.mkdir(exist_ok=True)
sys.path.insert(0,str(ROOT/'host'))
from device import Device,DeviceError
report={'real_voice':False,'physical_touch':False,'secured_wifi_join':False,'screens':[],'checks':[]}
def save():(OUT/'hardware-review.json').write_text(json.dumps(report,indent=2)+'\n')
with Device() as d:
 time.sleep(3);d.sync_clock();original=d.call('system.status');boot=d.boot_count
 assert original['version']==VERSION
 assert not original['password_length'] and not original['timer_remaining'] and not original['wifi_connecting'],'Preserve existing drafts/timers/connections'
 def capture(name):
  time.sleep(.35);d.screenshot(OUT/('after-'+name+'.png'));report['screens'].append(name);save();print(name,flush=True)
 def check(label,test):assert test,label;report['checks'].append(label);save()
 try:
  d.call('app.home');capture('home')
  for value in [0,100]:
   s=d.call('audio.volume.set',{'percent':value});check('volume '+str(value),s['volume']==value);capture('volume-'+str(value))
  for value in [10,100]:
   s=d.call('display.brightness.set',{'percent':value});time.sleep(.3);check('brightness '+str(value),d.call('system.status')['effective_brightness']==value);capture('brightness-'+str(value))
  for muted in [True,False]:
   s=d.call('audio.microphone.set',{'muted':muted});check('microphone '+str(muted),s['muted']==muted);capture('muted' if muted else 'mic-ready')
  d.call('timer.start',{'seconds':86400});capture('timer-24hours');d.call('timer.cancel')
  password='Example-Draft-'*4+'1234567';assert len(password)==63
  s=d.call('wifi.draft.set',{'ssid':'W'*32,'password':password});check('maximum draft',s['password_length']==63 and s['draft_ssid']=='W'*32);capture('wifi-maximum')
  s=d.call('wifi.password.edit',{'index':61,'delete_count':2,'insert':''});check('tail deletion caret',s['password_length']==61 and s['edit_index']==61 and s['edit_count']==0 and s['edit_removed']==2);capture('wifi-delete-tail')
  s=d.call('wifi.password.edit',{'index':61,'delete_count':0,'insert':'XY'});check('append range',s['password_length']==63 and s['edit_index']==61 and s['edit_count']==2 and s['edit_removed']==0);capture('wifi-append')
  s=d.call('wifi.password.edit',{'index':23,'delete_count':4,'insert':'ABC'});check('cross-line range',s['password_length']==62 and s['edit_index']==23 and s['edit_count']==3 and s['edit_removed']==4);capture('wifi-cross-line')
  s=d.call('wifi.password.edit',{'index':0,'delete_count':62,'insert':''});check('empty deletion caret',s['password_length']==0 and s['edit_index']==0 and s['edit_count']==0 and s['edit_removed']==62);capture('wifi-delete-all')
  n=d.call('wifi.password.edit',{'index':0,'delete_count':0,'insert':''});check('no-op leaves prior correction',all(n[k]==s[k] for k in ['caption','edit_index','edit_count','edit_removed','password_length']))
  d.call('wifi.draft.set',{'ssid':'Prototype review','password':'short'})
  try:d.call('wifi.connect');raise AssertionError('Short secured password accepted')
  except DeviceError as e:check('short password rejected','Use at least 8 characters' in str(e))
  s=d.call('system.status');check('short password feedback is truthful',s['caption']=='Use at least 8 characters' and s['wifi_error']==s['caption'] and not s['wifi_connecting']);capture('wifi-short-password')
  s=d.call('wifi.password.edit',{'index':5,'delete_count':0,'insert':'123'});check('correction clears validation error',not s['wifi_error'] and s['password_length']==8);capture('wifi-corrected')
  d.call('wifi.scan');chosen=d.call('wifi.draft.set',{'ssid':'Selected during scan','password':'prototype123'});d.wait_event('wifi.scan.result',20);s=d.call('system.status');check('late scan preserves selected draft',s['draft_ssid']==chosen['draft_ssid'] and s['caption']==chosen['caption']);capture('wifi-after-scan')
  d.call('wifi.scan');d.call('app.home');d.wait_event('wifi.scan.result',20);check('late scan preserves home',d.call('system.status')['app']=='home')
  for mode in ['listening','thinking','speaking','offline','idle']:
   s=d.call('voice.state.set',{'state':mode,'caption':mode.capitalize()});check('conversation '+mode,s['conversation']==mode);capture(mode)
  for label,x,y in [('left',0,280),('right',409,280),('top',205,0),('bottom',205,501)]:
   d.call('debug.touch',{'down':True,'x':205,'y':290})
   for i in range(1,9):d.call('debug.touch',{'down':True,'x':round(205+(x-205)*i/8),'y':round(290+(y-290)*i/8)})
   capture('extreme-'+label);d.call('debug.touch',{'down':False,'x':x,'y':y});time.sleep(.65)
  d.call('display.sleep');time.sleep(2.6);check('sleep parks renderer',d.call('system.status')['render_parked']);capture('sleep')
  d.call('debug.touch',{'down':True,'x':205,'y':290});d.call('debug.touch',{'down':False,'x':205,'y':290});time.sleep(.35);check('touch wakes',not d.call('system.status')['sleeping']);capture('wake')
  final=d.call('system.status');report['errors']={k:final[k]-original[k] for k in ['display_errors','audio_rx_errors','audio_tx_errors','audio_overflows','te_timeouts','scan_deadline_misses']};check('no new transport errors',not any(report['errors'].values()));check('no unexpected reboot',boot==d.boot_count);report['passed']=True;save()
 except Exception as e:report['error']=repr(e);report['logs']=d.logs;save();raise
 finally:
  d.call('debug.touch',{'down':False,'x':205,'y':290});d.call('voice.state.set',{'state':'idle'});d.call('timer.cancel')
  d.call('audio.volume.set',{'percent':original['volume']});d.call('display.brightness.set',{'percent':original['brightness']});d.call('audio.microphone.set',{'muted':original['muted']})
  if not d.call('system.status')['wifi_scanning']:d.call('wifi.scan');d.wait_event('wifi.scan.result',20)
  d.call('app.home');d.sync_clock()
