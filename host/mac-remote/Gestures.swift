import Foundation

enum Gesture: String { case begin, end, enter, undo }

struct Gestures {
    // Gravity-removed acceleration, in g. A light shake should work without a
    // hard snap; direction reversals (not force alone) distinguish a gesture.
    static let shakePeakG = 0.40
    static let shakeSequenceSeconds = 1.10
    static let shakePeakGapSeconds = 0.50
    var sequence: UInt32 = 0
    var initialized = false, down = false, suppressed = false, holding = false
    var lastTime = 0.0, pressedAt = 0.0, tapAt = -100.0
    var anchor = [0.0, 0.0], tapPoint = [0.0, 0.0]
    var peak = [0.0, 0.0, 0.0], peakAt = -100.0, shakeStart = 0.0, shakeUntil = 0.0
    var reversals = 0

    mutating func cancel() -> [Gesture] {
        let result: [Gesture] = holding ? [.end] : []
        down = false; holding = false; suppressed = true; tapAt = -100
        reversals = 0; peakAt = -100
        return result
    }
    mutating func advance(_ t: Double) -> [Gesture] {
        if down && !suppressed && !holding && t-pressedAt >= 0.24 {
            holding = true; tapAt = -100; return [.begin]
        }
        return []
    }
    mutating func touch(_ pressed: Bool, _ t: Double, _ x: Double, _ y: Double, _ ignore: Bool) -> [Gesture] {
        var result: [Gesture] = []
        if pressed {
            down = true; pressedAt = t; anchor = [x,y]; suppressed = ignore; holding = false
            if ignore { tapAt = -100 }
        } else if down {
            if !holding && hypot(x-anchor[0],y-anchor[1])>22 {suppressed=true}
            if holding { result.append(.end) }
            else if !suppressed && t-pressedAt >= 0.010 && t-pressedAt < 0.24 {
                if t-tapAt <= 0.40 && hypot(x-tapPoint[0],y-tapPoint[1]) <= 60 {
                    result.append(.enter);tapAt = -100
                } else { tapAt = t; tapPoint = [x,y] }
            } else { tapAt = -100 }
            down = false; holding = false; suppressed = false
        }
        return result
    }
    mutating func feed(_ packet: [String:Any]) -> [Gesture] {
        guard let us=packet["us"] as? Double, let number=packet["seq"] as? NSNumber else {return cancel()}
        let t=us/1e6, seq=number.uint32Value
        if !initialized || t < lastTime || seq < sequence || packet["overflow"] as? Bool == true || (lastTime > 0 && t-lastTime > 0.35) {
            let result=cancel();initialized=true;sequence=seq;lastTime=t
            return result
        }
        var result:[Gesture]=[]
        for e in packet["edges"] as? [[Double]] ?? [] where e.count == 7 && e[0] > Double(sequence) {
            result += touch(e[4] != 0,e[1]/1e6,e[2],e[3],e[5] != 0 || e[6] != 0)
        }
        sequence=seq;lastTime=t
        let x=packet["x"] as? Double ?? 0, y=packet["y"] as? Double ?? 0
        if down && !holding && hypot(x-anchor[0],y-anchor[1]) > 22 {suppressed=true;tapAt = -100}
        if packet["simulated"] as? Bool == true {result += cancel();return result}
        if down && packet["down"] as? Bool != true {result += cancel();return result}
        result += advance(t)
        if !down && packet["sleeping"] as? Bool != true && packet["motion_valid"] as? Bool == true,
           let v=packet["linear"] as? [Double], v.count == 3, v.allSatisfy({$0.isFinite}), t > shakeUntil {
            let strength=sqrt(v.reduce(0){$0+$1*$1})
            if t-shakeStart > Self.shakeSequenceSeconds {reversals=0;peakAt = -100}
            if strength >= Self.shakePeakG && t-peakAt >= 0.065 {
                let unit=v.map{$0/strength}
                if peakAt < 0 || t-peakAt > Self.shakePeakGapSeconds {reversals=0;shakeStart=t;peak=unit;peakAt=t}
                else if zip(unit,peak).reduce(0,{$0+$1.0*$1.1}) < -0.35 {
                    reversals+=1;peak=unit;peakAt=t
                    if reversals >= 2 {result.append(.undo);shakeUntil=t+2;reversals=0;peakAt = -100;tapAt = -100}
                }
            }
        } else {reversals=0;peakAt = -100}
        return result
    }
}

func gestureSelfTest() {
    func p(_ t:Double,_ seq:Int,_ down:Bool=false,_ edges:[[Double]]=[],_ linear:[Double]=[0,0,0],_ extra:[String:Any]=[:]) -> [String:Any] {
        var r:[String:Any]=["us":t*1e6,"seq":seq,"down":down,"edges":edges,"x":100.0,"y":100.0,"linear":linear,"motion_valid":true]
        r.merge(extra){_,new in new};return r
    }
    func edge(_ n:Int,_ t:Double,_ down:Bool,_ simulated:Bool=false,_ woke:Bool=false)->[Double] {[Double(n),t*1e6,100,100,down ? 1:0,simulated ? 1:0,woke ? 1:0]}
    var g=Gestures();precondition(g.feed(p(1,0)).isEmpty)
    precondition(g.feed(p(1.01,1,true,[edge(1,1.01,true)])).isEmpty)
    precondition(g.feed(p(1.26,1,true)) == [.begin])
    precondition(g.feed(p(1.30,2,false,[edge(2,1.29,false)])) == [.end])
    precondition(g.feed(p(1.50,4,false,[edge(3,1.42,true),edge(4,1.48,false)])).isEmpty)
    precondition(g.feed(p(1.70,6,false,[edge(5,1.60,true),edge(6,1.68,false)])) == [.enter])
    precondition(g.feed(p(1.80,6,false,[],[1.2,0,0])).isEmpty)
    precondition(g.feed(p(1.90,6,false,[],[-1.2,0,0])).isEmpty)
    precondition(g.feed(p(2.00,6,false,[],[1.2,0,0])) == [.undo])
    precondition(g.feed(p(2.10,6,false,[],[-1.2,0,0])).isEmpty)
    precondition(g.feed(p(2.2,7,true,[edge(7,2.2,true,false,true)])).isEmpty)
    precondition(g.feed(p(2.47,7,true)).isEmpty)
    precondition(g.feed(p(2.5,8,false,[edge(8,2.49,false)])).isEmpty)
    precondition(g.feed(p(2.6,9,true,[edge(9,2.6,true,true)])).isEmpty)
    precondition(g.feed(p(2.87,9,true,[],[0,0,0],["simulated":true])).isEmpty)
    _=g.feed(p(3,10,false,[edge(10,2.9,false,true)]))
    _=g.feed(p(3.1,11,true,[edge(11,3.1,true)]))
    precondition(g.feed(p(3.36,11,true)) == [.begin])
    precondition(g.feed(p(4,11,true)) == [.end]) // stale telemetry releases keys
    precondition(g.feed(p(0.2,0)).isEmpty) // reboot resets edge identity
    precondition(g.feed(p(0.3,0,false,[],[1.3,0,0])).isEmpty) // pickup alone isn't a shake
    var drag=Gestures();_=drag.feed(p(1,0));_=drag.feed(p(1.1,1,true,[edge(1,1.1,true)]))
    precondition(drag.feed(p(1.2,1,true,[],[0,0,0],["x":160.0])).isEmpty)
    precondition(drag.feed(p(1.4,1,true,[],[0,0,0],["x":160.0])).isEmpty)
    precondition(drag.feed(p(1.5,2,false,[edge(2,1.45,false)])).isEmpty)
    var fast=Gestures();_=fast.feed(p(1,0))
    precondition(fast.feed(p(1.08,4,false,[edge(1,1.01,true),edge(2,1.025,false),edge(3,1.05,true),edge(4,1.065,false)])) == [.enter])
    precondition(fast.feed(p(1.08,4,false,[edge(1,1.01,true),edge(2,1.025,false),edge(3,1.05,true),edge(4,1.065,false)])).isEmpty)
    precondition(decodeRemotePacket(Data(repeating:0,count:28),after:0)==nil)
    var bytes=Data(repeating:0,count:28);bytes[15]=1;bytes[14]=255
    precondition(decodeRemotePacket(bytes,after:0)?["battery"] as? Int == -1)
    bytes[13]=1;precondition(decodeRemotePacket(bytes,after:0)==nil)
    // Sampled motion, including both USB/BLE cadence and different phases:
    // lower force must work across axes, while noise/pickup/rotation must not.
    for hz in [1.6,2.0,3.0] {
        for dt in [0.02,0.04,0.06] {
            for phase in [0.0,0.7,1.5,2.3] {
                for axis in [[1.0,0,0],[0,1.0,0],[0,0,1.0],[0.6,0,0.8]] {
                    var light=Gestures();_=light.feed(p(1,0));var count=0
                    for i in 1...Int(1.4/dt) {
                        let t=Double(i)*dt, a=0.50*sin(2*Double.pi*hz*t+phase)
                        count += light.feed(p(1+t,0,false,[],axis.map{$0*a})).filter{$0 == .undo}.count
                    }
                    precondition(count==1,"Light shake missed or repeated")
                }
            }
        }
    }
    for mode in 0..<5 {
        var quiet=Gestures();_=quiet.feed(p(1,0))
        for i in 1...60 {
            let t=Double(i)*0.04
            let v: [Double]
            switch mode {
            case 0:v=[0.25*sin(t*18),0.08*cos(t*9),0] // small movements/noise
            case 1:v=t<0.20 ? [0.9,0,0]:t<0.40 ? [-0.7,0,0]:[0,0,0] // pickup/brake
            case 2:v=[0.7,0,0] // one sustained direction
            case 3:v=[0.7*max(0,cos(t*4)),0.7*max(0,sin(t*4)),0] // non-opposing directions
            default:v=[0.6*sin(t*18),0,0] // motion while sleeping
            }
            precondition(quiet.feed(p(1+t,0,false,[],v,mode==4 ? ["sleeping":true]:[:])).isEmpty)
        }
    }
    print("PASS: 144 light-shake cadence/axis/phase cases; 5 no-Undo motion cases")
    print("PASS: rapid 15ms double taps and dedup, BLE frame validation,")
    print("PASS: hold/release, buffered double-tap, deliberate shake, cooldown, wake-only touch, simulation exclusion, drag cancellation, stale/reboot reset")
}
