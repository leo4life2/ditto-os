import Cocoa
import ApplicationServices
import Carbon

struct Field {
    let element: AXUIElement
    let pid: pid_t
    let value: String?
}

func attribute(_ e:AXUIElement,_ name:String)->CFTypeRef? {
    // An unresponsive editor must not block input polling or modifier release.
    AXUIElementSetMessagingTimeout(e,0.025)
    var value:CFTypeRef?
    return AXUIElementCopyAttributeValue(e,name as CFString,&value) == .success ? value:nil
}

struct FocusSnapshot {
    let pid:pid_t?
    let field:Field?
    let secure:Bool
}

func captureFocus()->FocusSnapshot {
    let pid=NSWorkspace.shared.frontmostApplication?.processIdentifier
    var secure=IsSecureEventInputEnabled()
    var field:Field?
    if AXIsProcessTrusted(), !secure, let pid=pid {
        // Query the foreground app first. Some web-based composers don't
        // publish a system-wide focused field (or publish a proxy process).
        let app=AXUIElementCreateApplication(pid)
        let value=attribute(app,kAXFocusedUIElementAttribute)
            ?? attribute(AXUIElementCreateSystemWide(),kAXFocusedUIElementAttribute)
        if let value=value, CFGetTypeID(value)==AXUIElementGetTypeID() {
            let e=unsafeBitCast(value,to:AXUIElement.self)
            secure=attribute(e,kAXSubroleAttribute) as? String == kAXSecureTextFieldSubrole
            var owner:pid_t=0
            if !secure, AXUIElementGetPid(e,&owner) == .success, owner==pid {
                let text=attribute(e,kAXValueAttribute) as? String
                field=Field(element:e,pid:owner,value:(text?.utf8.count ?? 0)<1_000_000 ? text:nil)
            }
        }
    }
    return FocusSnapshot(pid:pid,field:field,secure:secure || IsSecureEventInputEnabled())
}

func focusedField()->Field? {captureFocus().field}
func same(_ a:Field,_ b:Field)->Bool {a.pid==b.pid && CFEqual(a.element,b.element)}

// Keyboard shortcut routing belongs to the foreground app. AX text inspection
// is optional bookkeeping for guarded Undo, not a prerequisite for dictation.
func routingDenial(_ focus:FocusSnapshot,currentPID:pid_t?,ownPID:pid_t,otherApps:Bool)->String? {
    guard let pid=focus.pid, pid==currentPID else{return "foreground app changed"}
    guard otherApps || pid==ownPID else{return "focus is outside the selected scope"}
    guard !focus.secure else{return "a secure input field is active"}
    return nil
}

func heldFocusChanged(_ heldPID:pid_t?,currentPID:pid_t?,secure:Bool)->Bool {
    guard let heldPID=heldPID else{return true}
    return secure || currentPID != heldPID
}

func focusSelfTest() {
    let own:pid_t=100,other:pid_t=200,proxy:pid_t=300
    let native=Field(element:AXUIElementCreateApplication(other),pid:other,value:"before")
    for field:Field? in [nil,native,Field(element:AXUIElementCreateApplication(proxy),pid:proxy,value:nil)] {
        let focus=FocusSnapshot(pid:other,field:field,secure:false)
        precondition(routingDenial(focus,currentPID:other,ownPID:own,otherApps:true)==nil)
        precondition(routingDenial(focus,currentPID:other,ownPID:own,otherApps:false) != nil)
        precondition(routingDenial(focus,currentPID:own,ownPID:own,otherApps:true) != nil)
    }
    precondition(routingDenial(FocusSnapshot(pid:own,field:nil,secure:false),currentPID:own,ownPID:own,otherApps:false)==nil)
    precondition(routingDenial(FocusSnapshot(pid:nil,field:nil,secure:false),currentPID:nil,ownPID:own,otherApps:true) != nil)
    for pid in [own,other] {
        precondition(routingDenial(FocusSnapshot(pid:pid,field:nil,secure:true),currentPID:pid,ownPID:own,otherApps:true) != nil)
    }
    precondition(!heldFocusChanged(other,currentPID:other,secure:false))
    precondition(heldFocusChanged(other,currentPID:own,secure:false))
    precondition(heldFocusChanged(other,currentPID:nil,secure:false))
    precondition(heldFocusChanged(nil,currentPID:other,secure:false))
    precondition(heldFocusChanged(other,currentPID:other,secure:true))
    print("PASS: missing/proxy AX focus permits shortcuts; scope, secure input and foreground-change guards remain")
}
