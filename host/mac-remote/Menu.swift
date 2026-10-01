import Cocoa

enum RemoteConnection:String,CaseIterable {
    case offline,usb,bluetooth
    var label:String {
        switch self {case .offline:return "Disconnected";case .usb:return "USB";case .bluetooth:return "Bluetooth"}
    }
    static func current(connected:Bool,path:String)->RemoteConnection {
        guard connected else{return .offline}
        switch path {case "USB":return .usb;case "Bluetooth":return .bluetooth;default:return .offline}
    }
}

func dittoSymbol(connection:RemoteConnection = .offline)->NSImage {
    let image=NSImage(size:NSSize(width:22,height:18),flipped:false){r in
        NSColor.black.set()
        // Trace the illustrated app icon's contour in its original 1280px
        // coordinates. Uniform scale preserves the head, nubs and broad base.
        func point(_ x:CGFloat,_ y:CGFloat)->NSPoint {
            NSPoint(x:1+(x-167)*20/924,y:17.4-(y-230)*20/924)
        }
        let p=NSBezierPath();p.move(to:point(604,230))
        func curve(_ x:CGFloat,_ y:CGFloat,_ ax:CGFloat,_ ay:CGFloat,_ bx:CGFloat,_ by:CGFloat){
            p.curve(to:point(x,y),controlPoint1:point(ax,ay),controlPoint2:point(bx,by))
        }
        curve(802,325,710,229,707,319)
        curve(896,424,878,326,879,352)
        curve(998,512,918,510,942,508)
        curve(1090,590,1062,512,1090,548)
        curve(1037,716,1090,641,1040,670)
        curve(1088,867,1033,760,1090,799)
        curve(937,982,1088,962,1038,982)
        curve(784,985,883,985,837,974)
        curve(571,996,710,1003,649,1008)
        curve(444,981,506,982,491,976)
        curve(210,962,350,991,254,993)
        curve(207,766,141,914,170,825)
        curve(279,620,244,708,280,673)
        curve(211,489,279,571,231,560)
        curve(282,346,186,420,229,346)
        curve(380,366,325,345,348,367)
        curve(604,230,447,369,469,232)
        p.close();p.fill()
        // Cut one optical-size connection glyph from the center of the same
        // silhouette. Template rendering supplies contrast in either theme.
        NSGraphicsContext.current?.compositingOperation = .destinationOut
        let glyph=NSBezierPath();glyph.lineWidth=1.2;glyph.lineCapStyle = .round;glyph.lineJoinStyle = .round
        switch connection {
        case .offline:
            glyph.move(to:NSPoint(x:9,y:8));glyph.line(to:NSPoint(x:14,y:8));glyph.stroke()
        case .usb:
            NSBezierPath(roundedRect:NSRect(x:8.5,y:7.5,width:6,height:4),xRadius:1,yRadius:1).fill()
            for x:CGFloat in [10,13] {
                glyph.move(to:NSPoint(x:x,y:11));glyph.line(to:NSPoint(x:x,y:13))
            }
            glyph.move(to:NSPoint(x:11.5,y:7.5));glyph.line(to:NSPoint(x:11.5,y:5.5))
            glyph.curve(to:NSPoint(x:13,y:4.5),controlPoint1:NSPoint(x:11.5,y:4.5),controlPoint2:NSPoint(x:12,y:4.5));glyph.stroke()
        case .bluetooth:
            glyph.move(to:NSPoint(x:8.5,y:5.5));glyph.line(to:NSPoint(x:14,y:10.5))
            glyph.line(to:NSPoint(x:11,y:13));glyph.line(to:NSPoint(x:11,y:3.5))
            glyph.line(to:NSPoint(x:14,y:6));glyph.line(to:NSPoint(x:8.5,y:11));glyph.stroke()
        }
        return true
    };image.isTemplate=true;image.accessibilityDescription="Ditto Remote — \(connection.label)";return image
}

func menuConnectionSelfTest(){
    // A remembered transport must never look connected after input goes stale.
    for path in ["USB","Bluetooth",""]{precondition(RemoteConnection.current(connected:false,path:path) == .offline)}
    precondition(RemoteConnection.current(connected:true,path:"USB") == .usb)
    precondition(RemoteConnection.current(connected:true,path:"Bluetooth") == .bluetooth)
    precondition(RemoteConnection.current(connected:true,path:"unknown") == .offline)
    print("PASS: menu connection states, stale transport, unknown transport")
}
final class RemoteHeader:NSView {
    let name=NSTextField(labelWithString:"Ditto Remote"),detail=NSTextField(labelWithString:"Disconnected"),dot=NSView()
    override init(frame:NSRect){super.init(frame:frame)
        let icon=NSImageView(frame:NSRect(x:15,y:16,width:32,height:32));icon.image=NSImage(named:NSImage.applicationIconName);addSubview(icon)
        name.frame=NSRect(x:58,y:34,width:194,height:19);name.font = .systemFont(ofSize:13,weight:.semibold);addSubview(name)
        detail.frame=NSRect(x:70,y:14,width:182,height:18);detail.font = .systemFont(ofSize:11);detail.textColor = .secondaryLabelColor;addSubview(detail)
        dot.frame=NSRect(x:60,y:20,width:6,height:6);dot.wantsLayer=true;dot.layer?.cornerRadius=3;addSubview(dot)
    }
    required init?(coder:NSCoder){fatalError()}
    func update(_ text:String,active:Bool){detail.stringValue=text;effectiveAppearance.performAsCurrentDrawingAppearance{dot.layer?.backgroundColor=(active ? NSColor.systemGreen:NSColor.secondaryLabelColor).cgColor}}
}

// Development-only rendering evidence from our own views. No input synthesis,
// system appearance changes, screen recording, or user-window capture.
func captureMenuForQA(){
    guard let i=CommandLine.arguments.firstIndex(of:"--qa-output"),CommandLine.arguments.count>i+1 else{return}
    let root=URL(fileURLWithPath:CommandLine.arguments[i+1])
    let timer=Timer(timeInterval:0.12,repeats:false){_ in
        try? FileManager.default.createDirectory(at:root,withIntermediateDirectories:true)
        var windows:[String]=[]
        for (n,w) in NSApp.windows.enumerated() where w.isVisible {
            windows.append("\(n) \(type(of:w)) \(w.frame)")
            guard let v=w.contentView,let bitmap=v.bitmapImageRepForCachingDisplay(in:v.bounds) else{continue}
            v.cacheDisplay(in:v.bounds,to:bitmap)
            if let png=bitmap.representation(using:.png,properties:[:]){try? png.write(to:root.appendingPathComponent("window-\(n).png"))}
        }
        try? windows.joined(separator:"\n").write(to:root.appendingPathComponent("windows.txt"),atomically:true,encoding:.utf8)
    }
    RunLoop.main.add(timer,forMode:.common)
}
