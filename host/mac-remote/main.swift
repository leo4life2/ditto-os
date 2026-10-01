import Cocoa
import ApplicationServices
import Carbon

if CommandLine.arguments.contains("--permission-status") {print(AXIsProcessTrusted() ? "Accessibility: enabled":"Accessibility: needs approval");exit(0)}

if CommandLine.arguments.contains("--self-test") {gestureSelfTest();menuConnectionSelfTest();feedbackSelfTest();bluetoothControlSelfTest();focusSelfTest();exit(0)}

if let i=CommandLine.arguments.firstIndex(of:"--bluetooth-probe"),CommandLine.arguments.count>i+1 {bluetoothProbe(seconds:Double(CommandLine.arguments[i+1]) ?? 30)}
if CommandLine.arguments.contains("--bluetooth") {UserDefaults.standard.set(true,forKey:"wireless")}

final class Remote: NSObject, NSApplicationDelegate, NSMenuDelegate {
    var window:NSWindow!, pad:NSTextView!, status:NSTextField!, note:NSTextField!, history:NSTextField!
    var microphoneStatus:NSTextField!
    var toggle:NSButton!, scope:NSPopUpButton!
    var restoreControl=UserDefaults.standard.bool(forKey:"controlEnabled")
    var feedback=EmoteFeedback()
    var timer:Timer?, machine=Gestures(), inFlight=false, enabled=false, keysDown=false
    var lastReply=Date.distantPast, rtt=0.0, connected=false
    var messages:[String]=[], target:Field?, ready:Field?, candidate:String?, changed=Date.distantPast, until=Date.distantPast
    var heldPID:pid_t?
    let session=URLSession(configuration:.ephemeral)
    var statusItem:NSStatusItem!,statusMenu:NSMenu!,menuHeader:RemoteHeader!,controlItem:NSMenuItem!
    var displayedConnection:RemoteConnection?
    let connectionSymbols=Dictionary(uniqueKeysWithValues:RemoteConnection.allCases.map{($0,dittoSymbol(connection:$0))})
    var audioTail=Date.distantPast,audioWanted=false,audioOutputReady=false,nextAudioAttempt=Date.distantPast
    var batteryWindow:NSWindow?
    let bluetooth=BluetoothLink()
    var inputPath="",lastUSB=Date.distantPast,lastBLE=Date.distantPast,battery = -1,bluetoothStatus="Bluetooth off"
    var nextStatus=Date.distantPast,nextBridgeAttempt=Date.distantPast,bridgeProcess:Process?
    var wireless:Bool {UserDefaults.standard.bool(forKey:"wireless")}
    func makeMenu(){
        statusItem=NSStatusBar.system.statusItem(withLength:NSStatusItem.variableLength)
        statusItem.button?.setAccessibilityLabel("Ditto Remote")
        statusMenu=NSMenu();statusMenu.minimumWidth=260;statusMenu.delegate=self
        if CommandLine.arguments.contains("--qa-appearance"){
            statusMenu.appearance=NSApp.appearance;statusItem.button?.appearance=NSApp.appearance
        }
        menuHeader=RemoteHeader(frame:NSRect(x:0,y:0,width:260,height:65));let h=NSMenuItem();h.view=menuHeader;statusMenu.addItem(h)
        statusMenu.addItem(.separator())
        controlItem=NSMenuItem(title:"Enable Remote",action:#selector(menuToggle),keyEquivalent:"");controlItem.target=self;controlItem.image=NSImage(systemSymbolName:"play.fill",accessibilityDescription:nil);statusMenu.addItem(controlItem)
        let bt=NSMenuItem(title:"Bluetooth",action:#selector(toggleBluetooth),keyEquivalent:"");bt.target=self;bt.tag=21;bt.image=NSImage(systemSymbolName:"antenna.radiowaves.left.and.right",accessibilityDescription:nil);statusMenu.addItem(bt)
        let test=NSMenuItem(title:"Open Test Window…",action:#selector(showWindow),keyEquivalent:"");test.target=self;test.image=NSImage(systemSymbolName:"text.cursor",accessibilityDescription:nil);statusMenu.addItem(test)
        let energy=NSMenuItem(title:"Battery Report…",action:#selector(showBatteryReport),keyEquivalent:"");energy.target=self;energy.image=NSImage(systemSymbolName:"battery.75percent",accessibilityDescription:nil);statusMenu.addItem(energy)
        let access=NSMenuItem(title:"Accessibility Permission…",action:#selector(permission),keyEquivalent:"");access.target=self;statusMenu.addItem(access)
        statusMenu.addItem(.separator())
        let quit=NSMenuItem(title:"Quit Ditto Remote",action:#selector(NSApplication.terminate(_:)),keyEquivalent:"q");statusMenu.addItem(quit)
        statusItem.menu=statusMenu;refreshMenu()
    }
    func refreshMenu(){
        let connection=RemoteConnection.current(connected:connected,path:inputPath)
        if displayedConnection != connection {
            statusItem?.button?.image=connectionSymbols[connection];displayedConnection=connection
        }
        let text=connected ? "\(inputPath) · \(enabled ? "Ready":"Paused")\(battery>=0 ? " · \(battery)%":"")":(wireless ? bluetoothStatus:"Disconnected")
        menuHeader?.update(text,active:connected&&enabled);statusItem?.button?.toolTip="Ditto Remote — \(text)"
        statusItem?.button?.setAccessibilityValue(text)
        controlItem?.title=enabled ? "Pause Remote":"Enable Remote"
        controlItem?.image=NSImage(systemSymbolName:enabled ? "pause.fill":"play.fill",accessibilityDescription:nil)
        statusMenu?.item(withTag:21)?.state=wireless ? .on:.off
    }
    func menuWillOpen(_ menu:NSMenu){refreshMenu();captureMenuForQA()}
    @objc func menuToggle(){toggle.state=enabled ? .off:.on;toggleControl();refreshMenu()}
    @objc func showMenu(){statusItem.button?.performClick(nil)}
    @objc func showWindow(){window.makeKeyAndOrderFront(nil);NSApp.activate(ignoringOtherApps:true)}
    func formattedReport(_ report:String)->NSAttributedString {
        let out=NSMutableAttributedString(string:"")
        for original in report.components(separatedBy:"\n"){
            if original.hasPrefix("| ---"){continue}
            var line=original.replacingOccurrences(of:"**",with:"")
            let paragraph=NSMutableParagraphStyle();paragraph.paragraphSpacing=7;paragraph.lineSpacing=3
            var font=NSFont.systemFont(ofSize:13)
            if line.hasPrefix("# "){line=String(line.dropFirst(2));font = .systemFont(ofSize:24,weight:.bold);paragraph.paragraphSpacing=16}
            else if line.hasPrefix("## "){line=String(line.dropFirst(3));font = .systemFont(ofSize:16,weight:.semibold);paragraph.paragraphSpacingBefore=12}
            else if line.hasPrefix("| "){
                let cells=line.split(separator:"|").map{$0.trimmingCharacters(in:.whitespaces)}
                line=cells.joined(separator:"\t");font = .monospacedDigitSystemFont(ofSize:12,weight:.regular)
                paragraph.tabStops=[NSTextTab(textAlignment:.left,location:300),NSTextTab(textAlignment:.left,location:400),NSTextTab(textAlignment:.left,location:500)]
            }else if line.hasPrefix("- "){line="• "+line.dropFirst(2)}
            out.append(NSAttributedString(string:line+"\n",attributes:[.font:font,.foregroundColor:NSColor.labelColor,.paragraphStyle:paragraph]))
        }
        return out
    }
    @objc func showBatteryReport(){
        if batteryWindow==nil {
            let w=NSWindow(contentRect:NSRect(x:0,y:0,width:720,height:600),styleMask:[.titled,.closable,.resizable],backing:.buffered,defer:false)
            w.isReleasedWhenClosed=false;w.title="Ditto · Battery Report";w.center();batteryWindow=w
            let scroll=NSScrollView(frame:w.contentView!.bounds);scroll.autoresizingMask=[.width,.height];scroll.hasVerticalScroller=true
            let text=NSTextView(frame:scroll.bounds);text.isEditable=false;text.isRichText=true;text.font = .systemFont(ofSize:13);text.textContainerInset=NSSize(width:20,height:20);text.autoresizingMask=[.width];scroll.documentView=text;w.contentView=scroll
        }
        batteryWindow!.makeKeyAndOrderFront(nil);NSApp.activate(ignoringOtherApps:true)
        session.dataTask(with:URL(string:"http://127.0.0.1:8781/battery")!){[weak self] data,_,_ in
            let value=data.flatMap{try? JSONSerialization.jsonObject(with:$0)} as? [String:Any]
            let fallback=FileManager.default.urls(for:.applicationSupportDirectory,in:.userDomainMask)[0].appendingPathComponent("Ditto Remote/Battery/Battery Report.md")
            let report=value?["report"] as? String ?? (try? String(contentsOf:fallback,encoding:.utf8)) ?? "No archived battery data yet. Connect Ditto with USB to collect its recorded wireless use."
            DispatchQueue.main.async{if let scroll=self?.batteryWindow?.contentView as? NSScrollView,let text=scroll.documentView as? NSTextView{text.textStorage?.setAttributedString(self?.formattedReport(report) ?? NSAttributedString(string:report))}}
        }.resume()
    }
    @objc func toggleBluetooth(){
        UserDefaults.standard.set(!wireless,forKey:"wireless")
        if wireless{bluetooth.start();startDeviceRadio()}else{bluetooth.stop();lastBLE = .distantPast}
        refreshMenu()
    }
    func startDeviceRadio(){
        // Explicit menu enable can retry a guarded radio startup over USB.
        // Never do this in the reconnect loop: preserve firmware crash recovery.
        var r=URLRequest(url:URL(string:"http://127.0.0.1:8781/action")!);r.httpMethod="POST";r.timeoutInterval=5
        r.setValue("application/json",forHTTPHeaderField:"Content-Type")
        r.httpBody=try? JSONSerialization.data(withJSONObject:["tool":"bluetooth.start","arguments":[:]] as [String:Any])
        session.dataTask(with:r){_,_,_ in}.resume()
    }
    func accept(_ p:[String:Any],path:String){
        if inputPath != path {release();machine=Gestures();ready=nil;target=nil;inputPath=path;log("Connected over \(path).")}
        connected=true;lastReply=Date()
        if restoreControl && AXIsProcessTrusted() {
            // Resume the same previously enabled scope after an app update.
            // Initialize input history before enabling: never replay a held
            // contact or buffered Enter from while the helper was absent.
            machine=Gestures();_=machine.feed(p);restoreControl=false
            enabled=true;toggle.state = .on
            note.stringValue="Control enabled. Hold Ditto to dictate; double-tap for Enter. Shake can undo a detected insertion."
        }
        if enabled && (p["down"] as? Bool)==true && (p["simulated"] as? Bool) != true && (p["sleeping"] as? Bool) != true {audioTail=Date().addingTimeInterval(1)}
        for action in machine.feed(p){perform(action)}
        if let b=p["battery"] as? Int{battery=b}
        refreshMenu()
    }
    func startBridge(){
        guard bridgeProcess?.isRunning != true,Date()>=nextBridgeAttempt else{return}
        nextBridgeAttempt=Date().addingTimeInterval(10)
        guard let python=Bundle.main.object(forInfoDictionaryKey:"ScoutPython") as? String,
              let script=Bundle.main.path(forResource:"bridge",ofType:"py",inDirectory:"host") else{return}
        let process=Process();process.executableURL=URL(fileURLWithPath:python);process.arguments=["-u",script]
        let support=FileManager.default.urls(for:.applicationSupportDirectory,in:.userDomainMask)[0].appendingPathComponent("Ditto Remote")
        do {try FileManager.default.createDirectory(at:support,withIntermediateDirectories:true)}catch{log("Could not prepare USB bridge.");return}
        process.environment=ProcessInfo.processInfo.environment.merging(["SCOUT_OUTPUT_DIR":support.path,"PYTHONDONTWRITEBYTECODE":"1"]){_,new in new}
        process.standardOutput=FileHandle.nullDevice;process.standardError=FileHandle.nullDevice
        process.terminationHandler={[weak self] p in DispatchQueue.main.async{self?.log("USB bridge stopped (\(p.terminationStatus)); reconnecting…")}}
        do {try process.run();bridgeProcess=process}catch{log("Could not start USB bridge.")}
    }
    func updateBattery(){
        guard Date()>=nextStatus else{return};nextStatus=Date().addingTimeInterval(3)
        var r=URLRequest(url:URL(string:"http://127.0.0.1:8781/status")!);r.timeoutInterval=1
        session.dataTask(with:r){[weak self] data,_,_ in
            guard let d=data,let p=(try? JSONSerialization.jsonObject(with:d)) as? [String:Any],let b=p["battery_percent"] as? Int else{return}
            DispatchQueue.main.async{self?.battery=b;self?.refreshMenu()}
        }.resume()
    }
    func applicationDidFinishLaunching(_ notification:Notification) {
        window=NSWindow(contentRect:NSRect(x:0,y:0,width:650,height:560),styleMask:[.titled,.closable,.miniaturizable,.resizable],backing:.buffered,defer:false)
        window.isReleasedWhenClosed=false
        window.title="Ditto Remote · Test Window";window.center()
        let stack=NSStackView();stack.orientation = .vertical;stack.alignment = .leading;stack.spacing=14;stack.translatesAutoresizingMaskIntoConstraints=false
        window.contentView!.addSubview(stack)
        NSLayoutConstraint.activate([stack.leadingAnchor.constraint(equalTo:window.contentView!.leadingAnchor,constant:24),stack.trailingAnchor.constraint(equalTo:window.contentView!.trailingAnchor,constant:-24),stack.topAnchor.constraint(equalTo:window.contentView!.topAnchor,constant:24)])
        let heading=NSTextField(labelWithString:"Ditto Remote");heading.font = .systemFont(ofSize:26,weight:.bold);stack.addArrangedSubview(heading)
        status=NSTextField(labelWithString:"Connecting to the USB bridge…");stack.addArrangedSubview(status)
        microphoneStatus=NSTextField(labelWithString:"Microphone: USB · wireless audio preparing…");microphoneStatus.font = .systemFont(ofSize:12);microphoneStatus.textColor = .secondaryLabelColor;stack.addArrangedSubview(microphoneStatus)
        let help=NSTextField(wrappingLabelWithString:"Hold still to dictate · release to insert\nDouble-tap for Enter · light back-and-forth shake for Undo\nDragging still plays with Ditto. The first touch after sleep only wakes it.");stack.addArrangedSubview(help)
        let row=NSStackView();row.orientation = .horizontal;row.spacing=14
        scope=NSPopUpButton();scope.addItems(withTitles:["Scratchpad only","Other focused apps"]);scope.selectItem(at:UserDefaults.standard.integer(forKey:"scope"));scope.target=self;scope.action=#selector(scopeChanged);row.addArrangedSubview(scope)
        toggle=NSButton(checkboxWithTitle:"Enable keyboard control",target:self,action:#selector(toggleControl));row.addArrangedSubview(toggle)
        stack.addArrangedSubview(row)
        let permission=NSButton(title:"Allow Accessibility…",target:self,action:#selector(permission));stack.addArrangedSubview(permission)
        note=NSTextField(wrappingLabelWithString:"Gesture test is active. No keystrokes are sent until you enable control.");stack.addArrangedSubview(note)
        let scroll=NSScrollView();scroll.hasVerticalScroller=true;scroll.borderType = .bezelBorder
        pad=NSTextView(frame:NSRect(x:0,y:0,width:590,height:150));pad.isRichText=false;pad.font = .systemFont(ofSize:18);pad.allowsUndo=true;pad.textContainerInset=NSSize(width:12,height:12)
        scroll.documentView=pad;scroll.translatesAutoresizingMaskIntoConstraints=false;stack.addArrangedSubview(scroll)
        NSLayoutConstraint.activate([scroll.widthAnchor.constraint(equalTo:stack.widthAnchor),scroll.heightAnchor.constraint(equalToConstant:155)])
        history=NSTextField(wrappingLabelWithString:"Gesture events will appear here.");history.font = .monospacedSystemFont(ofSize:11,weight:.regular);stack.addArrangedSubview(history)
        let menu=NSMenu();let appMenu=NSMenu();let reveal=NSMenuItem(title:"Show Ditto Menu",action:#selector(showMenu),keyEquivalent:"m");reveal.keyEquivalentModifierMask=[.command,.shift];reveal.target=self;appMenu.addItem(reveal);appMenu.addItem(withTitle:"Quit Scout Remote",action:#selector(NSApplication.terminate(_:)),keyEquivalent:"q");let item=NSMenuItem();item.submenu=appMenu;menu.addItem(item)
        let edit=NSMenu();edit.addItem(withTitle:"Undo",action:Selector(("undo:")),keyEquivalent:"z");edit.addItem(withTitle:"Cut",action:#selector(NSText.cut(_:)),keyEquivalent:"x");edit.addItem(withTitle:"Copy",action:#selector(NSText.copy(_:)),keyEquivalent:"c");edit.addItem(withTitle:"Paste",action:#selector(NSText.paste(_:)),keyEquivalent:"v");edit.addItem(withTitle:"Select All",action:#selector(NSText.selectAll(_:)),keyEquivalent:"a");let editItem=NSMenuItem(title:"Edit",action:nil,keyEquivalent:"");editItem.submenu=edit;menu.addItem(editItem);NSApp.mainMenu=menu
        if let icon=NSImage(contentsOfFile:Bundle.main.path(forResource:"AppIcon",ofType:"icns") ?? ""){NSApp.applicationIconImage=icon}
        makeMenu()
        bluetooth.receivedAudio={data in data.withUnsafeBytes{p in _=ditto_audio_packet(p.bindMemory(to:UInt8.self).baseAddress,data.count)}}
        bluetooth.acknowledged={[weak self] seq in self?.feedback.acknowledge(NSNumber(value:seq))}
        bluetooth.changed={[weak self] value in self?.bluetoothStatus=value;self?.refreshMenu()}
        bluetooth.receive={[weak self] data in
            guard let self=self else{return};self.lastBLE=Date()
            guard Date().timeIntervalSince(self.lastUSB)>0.35,let p=decodeRemotePacket(data,after:self.machine.sequence) else{return}
            self.accept(p,path:"Bluetooth");self.status.stringValue="Bluetooth connected · \(AXIsProcessTrusted() ? "keyboard permission ready":"keyboard permission needed")"
        }
        if wireless{bluetooth.start()}
        if !UserDefaults.standard.bool(forKey:"hasOpened") || !AXIsProcessTrusted(){showWindow();window.makeFirstResponder(pad)}
        UserDefaults.standard.set(true,forKey:"hasOpened")
        timer=Timer(timeInterval:0.04,repeats:true){[weak self] _ in self?.tick()};RunLoop.main.add(timer!,forMode:.common)
    }
    func log(_ text:String) {messages.append(text);messages=Array(messages.suffix(5));history.stringValue=messages.joined(separator:"\n")}
    @objc func permission() {
        let options=[kAXTrustedCheckOptionPrompt.takeUnretainedValue() as String:true] as CFDictionary
        _=AXIsProcessTrustedWithOptions(options)
        NSWorkspace.shared.open(URL(string:"x-apple.systempreferences:com.apple.preference.security?Privacy_Accessibility")!)
        note.stringValue="Enable Ditto Remote (previously Scout Remote) in macOS Accessibility, then return and enable keyboard control."
    }
    @objc func toggleControl() {
        release();target=nil;ready=nil;machine=Gestures();restoreControl=false
        enabled=toggle.state == .on && AXIsProcessTrusted() && connected
        UserDefaults.standard.set(enabled,forKey:"controlEnabled")
        if !enabled {toggle.state = .off;note.stringValue=AXIsProcessTrusted() ? "Gesture test only. Connect the device before enabling keys.":"macOS Accessibility permission is required to send keys. Use Allow Accessibility."}
        else {note.stringValue="Control enabled. Hold Ditto to dictate; double-tap for Enter. Shake can undo a detected insertion.";window.makeFirstResponder(pad)}
    }
    @objc func scopeChanged() {UserDefaults.standard.set(scope.indexOfSelectedItem,forKey:"scope");toggle.state = .off;toggleControl();log("Scope changed; control disabled until explicitly re-enabled.")}
    func allowed(_ field:Field)->Bool {
        let front=NSWorkspace.shared.frontmostApplication?.processIdentifier
        return front==field.pid && (scope.indexOfSelectedItem==1 || field.pid==ProcessInfo.processInfo.processIdentifier)
    }
    func modifier(_ code:CGKeyCode,_ flags:CGEventFlags,_ down:Bool) {
        guard let e=CGEvent(keyboardEventSource:nil,virtualKey:code,keyDown:down) else {return}
        e.type = .flagsChanged;e.flags=flags;e.post(tap:.cghidEventTap)
    }
    func release() {
        if keysDown {modifier(58,.maskControl,false);modifier(59,[],false);keysDown=false}
        heldPID=nil
    }
    func key(_ code:CGKeyCode,_ flags:CGEventFlags=[]) {
        for down in [true,false] {let e=CGEvent(keyboardEventSource:nil,virtualKey:code,keyDown:down);e?.flags=flags;e?.post(tap:.cghidEventTap)}
    }
    func perform(_ action:Gesture) {
        log("\(enabled ? "Control":"Test") · \(action.rawValue)")
        if action == .end {
            let wasDown=keysDown;release()
            if wasDown {until=Date().addingTimeInterval(15);candidate=nil;changed=Date();if !machine.suppressed{feedback.append("end")}}
            return
        }
        guard enabled && AXIsProcessTrusted() else {return}
        let focus=captureFocus()
        if let reason=routingDenial(focus,currentPID:NSWorkspace.shared.frontmostApplication?.processIdentifier,
                                   ownPID:ProcessInfo.processInfo.processIdentifier,otherApps:scope.indexOfSelectedItem==1) {
            log("Ignored: \(reason).");return
        }
        if action == .begin {
            // Never take ownership of modifiers already held by the physical keyboard.
            guard !CGEventSource.keyState(.combinedSessionState,key:58), !CGEventSource.keyState(.combinedSessionState,key:59) else {log("Ignored: Control or Option is already held.");return}
            feedback.cancel()
            target=focus.field;heldPID=focus.pid;ready=nil;candidate=nil;until = .distantPast
            modifier(59,.maskControl,true);modifier(58,[.maskControl,.maskAlternate],true);keysDown=true
            note.stringValue="Dictating… release Ditto to finish."
            log(focus.field?.value == nil ? "Shortcut active · editor text unavailable for Undo.":"Shortcut active · editor text available for Undo.")
        } else if action == .enter {
            // Return is an ordinary keyboard command, independent of whether
            // Flow just inserted text or the previous transcript was undone.
            guard !keysDown else {return}
            key(36);feedback.append("enter");note.stringValue="Enter sent.";log("Enter sent.")
            ready=nil;target=nil;until = .distantPast
        } else if action == .undo {
            guard !keysDown, let field=focus.field, allowed(field), let ready=ready, same(ready,field), let expected=ready.value, expected==field.value else {log("Ignored: no unchanged completed insertion in this field.");return}
            key(6,.maskCommand);feedback.append("undo");note.stringValue="Undo sent. The focused app controls its undo grouping."
            self.ready=nil;target=nil;until = .distantPast
        }
    }
    func tick() {
        if Date().timeIntervalSince(lastReply)>0.35 {
            if connected {release();machine=Gestures();ready=nil;target=nil;log("Device input stale: held keys released.")}
            connected=false;refreshMenu()
        }
        if keysDown && heldFocusChanged(heldPID,currentPID:NSWorkspace.shared.frontmostApplication?.processIdentifier,secure:IsSecureEventInputEnabled()) {
            release();_=machine.cancel();target=nil;ready=nil;log("Focus changed: recording keys released.")
        }
        if !keysDown && Date()<until, let original=target {
            if let field=focusedField(), same(original,field), allowed(field), let current=field.value, let before=original.value {
                if current != before {
                    if candidate != current {candidate=current;changed=Date()}
                    else if Date().timeIntervalSince(changed)>0.45 {ready=field;until = .distantPast;note.stringValue="Text inserted. Double-tap for Enter, or shake to Undo."}
                }
            } else {until = .distantPast;target=nil}
        }
        if wireless && bluetooth.audioReady && Date()>=nextAudioAttempt && !audioOutputReady {
            nextAudioAttempt=Date().addingTimeInterval(5);audioOutputReady=ditto_audio_start()==0
        }
        microphoneStatus?.stringValue=wireless ? String(cString:ditto_audio_status()):"Wireless microphone off · use Ditto USB input"
        let want=enabled && connected && wireless && bluetooth.audioReady && audioOutputReady && (keysDown || Date()<audioTail)
        if want && !audioWanted{ditto_audio_reset()};audioWanted=want
        if connected && wireless {bluetooth.send(dictating:keysDown,client:feedback.client,event:inputPath=="Bluetooth" ? feedback.next():nil,audio:want)}
        if connected && inputPath=="USB"{updateBattery()}
        guard !CommandLine.arguments.contains("--bluetooth-only"),!inFlight else {return};inFlight=true
        var request=URLRequest(url:URL(string:"http://127.0.0.1:8781/action")!);request.httpMethod="POST";request.timeoutInterval=0.3;request.setValue("application/json",forHTTPHeaderField:"Content-Type")
        var arguments:[String:Any]=["after":machine.sequence]
        if inputPath != "Bluetooth" {arguments["dictating"]=keysDown;if let event=feedback.next(){arguments["feedback"]=event}}
        request.httpBody=try! JSONSerialization.data(withJSONObject:["tool":"input.remote","arguments":arguments])
        let began=Date()
        session.dataTask(with:request){[weak self] data,response,error in
            DispatchQueue.main.async {
                guard let self=self else {return};self.inFlight=false
                guard let data=data, let http=response as? HTTPURLResponse,http.statusCode==200,
                      let p=(try? JSONSerialization.jsonObject(with:data)) as? [String:Any],p["us"] is NSNumber,
                      Date().timeIntervalSince(began)<0.3 else {
                    if response == nil,let e=error as? URLError,e.code == .cannotConnectToHost{self.startBridge()}
                    if !self.connected{self.status.stringValue=self.wireless ? self.bluetoothStatus:"Connect Ditto with USB or enable Bluetooth in the menu."}
                    return
                }
                self.feedback.acknowledge(p["feedback_ack"])
                self.lastUSB=Date();self.rtt=Date().timeIntervalSince(began)*1000
                self.status.stringValue=String(format:"USB connected · %.0f ms round trip · %@",self.rtt,AXIsProcessTrusted() ? "keyboard permission ready":"keyboard permission needed")
                self.accept(p,path:"USB")
            }
        }.resume()
    }
    func applicationShouldHandleReopen(_ sender:NSApplication,hasVisibleWindows flag:Bool)->Bool {
        showWindow();return true
    }
    func applicationShouldTerminateAfterLastWindowClosed(_ sender:NSApplication)->Bool {false}
    func applicationWillTerminate(_ notification:Notification) {release();timer?.invalidate();session.invalidateAndCancel();bluetooth.stop();ditto_audio_stop();bridgeProcess?.terminate()}
}
let app=NSApplication.shared
if let i=CommandLine.arguments.firstIndex(of:"--qa-appearance"),CommandLine.arguments.count>i+1 {
    app.appearance=NSAppearance(named:CommandLine.arguments[i+1]=="light" ? .aqua:.darkAqua)
}
let delegate=Remote();app.delegate=delegate;app.setActivationPolicy(.accessory);app.run()
