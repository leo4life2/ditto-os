"""Exercise production gesture routing with deterministic focus and a key-event spy.

No system key events, UI automation, or editor contents are accessed. Only the
OS boundary functions are substituted in a disposable native test executable.
"""
from pathlib import Path
import subprocess, sys

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from storage_hygiene import scratch_directory

source=ROOT/'host/mac-remote'
main=(source/'main.swift').read_text().split('let app=NSApplication.shared\n')[0]
main=main.replace('NSWorkspace.shared.frontmostApplication?.processIdentifier','testFront')
main=main.replace('AXIsProcessTrusted()','true')
main=main.replace('IsSecureEventInputEnabled()','testSecure')
main=main.replace('CGEventSource.keyState(.combinedSessionState,key:58)','false')
main=main.replace('CGEventSource.keyState(.combinedSessionState,key:59)','false')
a=main.index('    func modifier(');b=main.index('    func release()',a)
main=main[:a]+'''    func modifier(_ code:CGKeyCode,_ flags:CGEventFlags,_ down:Bool) {
        events.append("m:\\(code):\\(down)")
    }
'''+main[b:]
a=main.index('    func key(');b=main.index('    func perform(',a)
main=main[:a]+'''    func key(_ code:CGKeyCode,_ flags:CGEventFlags=[]) {events.append("k:\\(code)")}
'''+main[b:]
focus=(source/'Focus.swift').read_text().replace('func captureFocus()->FocusSnapshot {','func unusedLiveFocus()->FocusSnapshot {')
harness=r'''
var testFront:pid_t?=200
var testSecure=false
var testField:Field?
var events:[String]=[]
func captureFocus()->FocusSnapshot {FocusSnapshot(pid:testFront,field:testField,secure:testSecure)}
let app=NSApplication.shared
let remote=Remote()
remote.status=NSTextField(labelWithString:"")
remote.note=NSTextField(labelWithString:"")
remote.history=NSTextField(labelWithString:"")
remote.scope=NSPopUpButton()
remote.scope.addItems(withTitles:["Scratchpad only","Other focused apps"])
remote.scope.selectItem(at:1)
remote.enabled=true;remote.inFlight=true

func reset() {
    remote.release();remote.target=nil;remote.ready=nil;remote.until = .distantPast
    remote.feedback.cancel();remote.machine=Gestures();events=[]
    remote.enabled=true;remote.connected=true;remote.lastReply=Date()
    testFront=200;testSecure=false;testField=nil;remote.scope.selectItem(at:1)
}
reset()
remote.perform(.begin)
precondition(remote.keysDown && remote.heldPID==200 && remote.target==nil)
precondition(events==["m:59:true","m:58:true"])
remote.perform(.end)
precondition(!remote.keysDown && remote.heldPID==nil)
precondition(events==["m:59:true","m:58:true","m:58:false","m:59:false"])
precondition(remote.feedback.next()?["kind"] as? String == "end")
remote.perform(.undo);precondition(events.count==4)
remote.perform(.enter);precondition(events.last=="k:36")
print("PASS: AX-unavailable hold/release and Enter dispatch; Undo remains blocked")

reset();remote.perform(.begin);testFront=300;remote.tick()
precondition(!remote.keysDown && remote.heldPID==nil && events.count==4)
reset();remote.perform(.begin);testFront=nil;remote.tick()
precondition(!remote.keysDown && events.count==4)
reset();remote.perform(.begin);remote.lastReply = .distantPast;remote.tick()
precondition(!remote.keysDown && !remote.connected && events.count==4)
reset();remote.perform(.begin);testSecure=true;remote.tick()
precondition(!remote.keysDown && events.count==4)
reset();testSecure=true;remote.perform(.begin);remote.perform(.enter)
precondition(events.isEmpty)
reset();remote.scope.selectItem(at:0);remote.perform(.begin);remote.perform(.enter)
precondition(events.isEmpty)
print("PASS: app-change/no-foreground/stale/secure release; secure and scratchpad scope rejection")

reset()
let element=AXUIElementCreateApplication(200)
testField=Field(element:element,pid:200,value:"before")
remote.perform(.begin);precondition(remote.target?.value=="before")
remote.perform(.end)
testField=Field(element:element,pid:200,value:"before insertion")
remote.candidate="before insertion";remote.changed=Date().addingTimeInterval(-1)
remote.tick();precondition(remote.ready?.value=="before insertion")
remote.perform(.undo);precondition(events.last=="k:6")
let count=events.count;remote.perform(.undo);precondition(events.count==count)
remote.ready=Field(element:element,pid:200,value:"old insertion")
remote.perform(.undo);precondition(events.count==count)
print("PASS: unchanged insertion Undo succeeds once; later edits block Undo")

// Run the real gesture recognizer into the real dispatcher with AX unavailable.
reset()
func packet(_ t:Double,_ seq:Int,_ down:Bool,_ edges:[[Double]]=[])->[String:Any] {
    ["us":t*1e6,"seq":seq,"down":down,"x":100.0,"y":100.0,"edges":edges]
}
for p in [packet(1,0,false),packet(1.01,1,true,[[1,1010000,100,100,1,0,0]]),
          packet(1.27,1,true),packet(1.32,2,false,[[2,1320000,100,100,0,0,0]])] {
    for action in remote.machine.feed(p){remote.perform(action)}
}
precondition(events==["m:59:true","m:58:true","m:58:false","m:59:false"])
precondition(!remote.keysDown)
print("PASS: real hold recognizer → dispatcher → exact balanced modifier sequence with no AX field")
'''
with scratch_directory('mac-focus-check',reserve_mib=32) as scratch:
    (scratch/'Focus.swift').write_text(focus)
    (scratch/'main.swift').write_text(main+'\n'+harness)
    obj=scratch/'audio.o'
    subprocess.run(['clang++','-std=c++17','-O2','-c',str(source/'WirelessAudio.mm'),'-o',str(obj)],check=True)
    subprocess.run(['swiftc','-import-objc-header',str(source/'WirelessAudio.h'),str(obj),'-lc++','-framework','AudioToolbox','-framework','CoreAudio','-O','-framework','Cocoa','-framework','ApplicationServices',
                    '-framework','CoreBluetooth','-framework','Carbon',
                    *[str(source/f) for f in ['Gestures.swift','Feedback.swift','Bluetooth.swift','BluetoothProbe.swift','Menu.swift']],
                    str(scratch/'Focus.swift'),str(scratch/'main.swift'),'-o',str(scratch/'check')],check=True)
    subprocess.run([str(scratch/'check')],check=True)
