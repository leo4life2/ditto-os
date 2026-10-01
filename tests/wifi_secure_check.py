"""Secured Wi-Fi acceptance with an owned test network and a password prompt.

Stop the USB bridge first. Run: python tests/wifi_secure_check.py --ssid NAME
This requires an unused network configuration and leaves the verified network
saved/connected. Passwords are never included in reports, logs or captures.
"""
import argparse,getpass,json,sys,time
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT/'host'),str(ROOT/'tools')]
from device import Device,DeviceError
from validation_context import evidence_directory

def wait_state(d,predicate,timeout):
    deadline=time.monotonic()+timeout
    while time.monotonic()<deadline:
        try:
            s=d.call('system.status',timeout=3)
            if predicate(s):return s
        except DeviceError:pass  # Read-only observation can retry across a reboot.
        time.sleep(.2)
    raise AssertionError('Expected network state did not arrive before the deadline')

def connect(d,ssid,password,correct_draft=False):
    if correct_draft:
        replacement='x' if password[-1]!='x' else 'y'
        d.call('wifi.draft.set',{'ssid':ssid,'password':password[:-1]+replacement})
        d.call('wifi.password.edit',{'index':len(password)-1,'delete_count':1,'insert':password[-1]})
    else:d.call('wifi.draft.set',{'ssid':ssid,'password':password})
    pending=d.call('wifi.connect')
    assert pending['wifi_connecting'] or pending['wifi_connected'], 'No pending/success state'
    assert pending['password_length']==0,'Password was not cleared from the visible draft'
    d.call('app.open',{'name':'volume'})
    s=wait_state(d,lambda s:s['wifi_connected'] or bool(s['wifi_error']),36)
    assert s['app']=='volume','Asynchronous result replaced a newer surface'
    return s

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--ssid',required=True,help='A secured test network you own, visible to this board')
    args=parser.parse_args()
    if not 1<=len(args.ssid.encode())<=32:parser.error('SSID must occupy 1–32 UTF-8 bytes')
    password=getpass.getpass('Test network password (not saved on the Mac): ')
    if not password.isascii() or not 8<=len(password)<=63:parser.error('This prototype test needs an 8–63 character ASCII passphrase')
    out=evidence_directory(ROOT);report={'ssid':args.ssid,'passed':False,'checks':{},'password_recorded':False}
    configuration_started=False;good_password_proven=False
    try:
        with Device() as d:
            time.sleep(2);original=d.call('system.status')
            report['firmware_version']=original['version']
            assert not original['ssid'],'Saved network exists; preserve it and use a separate controlled test plan'
            assert not original['password_length'] and original['timer_remaining']==0,'An active draft or timer must finish first'
            d.call('wifi.scan');scan=d.wait_event('wifi.scan.result',20)
            assert scan['ok'] and any(n['ssid']==args.ssid and n['secured'] for n in scan['networks']),'The specified secured network was not observed; no connection attempted'
            report['checks']['secured_target_observed']=True
            try:
                configuration_started=True
                s=connect(d,args.ssid,password,correct_draft=True)
                assert s['wifi_connected'] and not s['wifi_error'] and s['ssid']==args.ssid,'Corrected password did not establish a connection'
                good_password_proven=True
                report['checks']['corrected_password_join']=True
                d.call('app.open',{'name':'wifi'});d.screenshot(out/'secured-wifi-connected.png')
                d.call('wifi.disconnect');time.sleep(2)
                s=d.call('system.status')
                assert not s['wifi_connected'] and not s['wifi_connecting'] and s['ssid']==args.ssid
                report['checks']['explicit_disconnect_preserves_saved_network']=True
                wrong='Scout-intentionally-wrong-key-6a9e'
                if wrong==password:wrong+='x'
                s=connect(d,args.ssid,wrong)
                assert not s['wifi_connected'] and s['wifi_error'] and not s['wifi_connecting'],'Incorrect password was not reported as a bounded failure'
                report['checks']['wrong_password_failure']={'message':s['wifi_error']}
                d.call('app.open',{'name':'wifi'});d.screenshot(out/'secured-wifi-wrong-password.png')
                s=connect(d,args.ssid,password)
                assert s['wifi_connected'] and not s['wifi_error']
                report['checks']['correction_after_failure_recovers']=True
                d.call('wifi.disconnect');time.sleep(.3)
                d.call('system.reboot');time.sleep(2)
                s=wait_state(d,lambda s:s['wifi_connected'] and s['ssid']==args.ssid,36)
                report['checks']['saved_network_rejoins_after_reboot']=True
                d.sync_clock();d.call('app.home')
                report['checks']['foreground_preserved_on_success_and_failure']=True
                report['passed']=True
            finally:
                # This plan starts from no saved network. If initial credentials
                # did not work, remove only the configuration this test created.
                if configuration_started and not good_password_proven:
                    d.call('wifi.forget');d.call('app.home')
                elif good_password_proven and not report['passed']:
                    restored=connect(d,args.ssid,password)
                    report['restored_working_network']=bool(restored['wifi_connected'])
                    d.call('app.home')
    except Exception as error:
        report['failure_type']=type(error).__name__
        report['failure']=str(error)
        raise
    finally:
        report['limits']='Uses real secured association and reboot reconnect when executed. Does not prove router-outage recovery or an explicit saved-network reconnect action.'
        (out/'secured-wifi-check.json').write_text(json.dumps(report,indent=2)+'\n')
        print(json.dumps(report,indent=2))

if __name__=='__main__':main()
