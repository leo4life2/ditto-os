import Foundation

// Piggyback on the existing, ordered input poll. Acknowledgement prevents a
// lost HTTP reply from replaying an emote; stale offline actions expire.
struct EmoteFeedback {
    struct Event {let seq:UInt32;let kind:String;let expires:Date}
    let client=UUID().uuidString
    var sequence:UInt32=0,events:[Event]=[]
    mutating func append(_ kind:String,now:Date=Date()) {
        precondition(["end","enter","undo"].contains(kind))
        sequence &+= 1
        events.append(Event(seq:sequence,kind:kind,expires:now.addingTimeInterval(2)))
        events=Array(events.suffix(8))
    }
    mutating func cancel(){events.removeAll()}
    mutating func next(now:Date=Date())->[String:Any]? {
        events.removeAll{$0.expires<now}
        guard let e=events.first else{return nil}
        return ["client":client,"seq":e.seq,"kind":e.kind]
    }
    mutating func acknowledge(_ value:Any?) {
        guard let n=value as? NSNumber else{return}
        events.removeAll{$0.seq<=n.uint32Value}
    }
}
func feedbackSelfTest() {
    var f=EmoteFeedback();let t=Date(timeIntervalSince1970:100)
    f.append("end",now:t);f.append("enter",now:t)
    precondition(f.next(now:t)?["kind"] as? String == "end")
    precondition(f.next(now:t)?["seq"] as? UInt32 == 1) // retry same ID
    f.acknowledge(NSNumber(value:1))
    precondition(f.next(now:t)?["kind"] as? String == "enter")
    f.acknowledge(NSNumber(value:1)) // old ACK cannot clear newer action
    precondition(f.events.count==1)
    precondition(f.next(now:t.addingTimeInterval(3))==nil)
    f.append("undo",now:t);f.cancel();precondition(f.next(now:t)==nil)
    for _ in 0..<100{f.append("enter",now:t)}
    precondition(f.events.count==8)
    print("PASS: ordered emote feedback, retry identity, ACK, expiry, cancellation, bounded queue")
}
