import Foundation
// Same signed application identity as the helper. No keyboard events or audio;
// exercises the actual bonded GATT channel and emits bounded diagnostic data.
func bluetoothProbe(seconds:Double) {
    let link=BluetoothLink(),start=Date(),client=UUID().uuidString
    var packets=0,acks=0,intervals:[Double]=[],last=Date.distantPast,sent:Date?,maxACK=0.0
    func log(_ value:[String:Any]){if let d=try? JSONSerialization.data(withJSONObject:value,options:[.sortedKeys]){FileHandle.standardOutput.write(d);FileHandle.standardOutput.write(Data([10]))}}
    let audioTest=CommandLine.arguments.contains("--audio")
    let fileIndex=CommandLine.arguments.firstIndex(of:"--pcm")
    let filePath=fileIndex.flatMap{CommandLine.arguments.count>$0+1 ? CommandLine.arguments[$0+1]:nil}
    if let path=filePath{FileManager.default.createFile(atPath:path,contents:nil)}
    let pcmFile=filePath.flatMap{FileHandle(forWritingAtPath:$0)}
    if audioTest{let result=ditto_audio_start();log(["audio_output":String(cString:ditto_audio_status()),"result":result])}
    var audioPackets=0,audioFirst:Double = -1,lastAudio=Date.distantPast,audioGaps:[Double]=[]
    link.receivedAudio={data in
        let now=Date();if audioPackets>0{audioGaps.append(now.timeIntervalSince(lastAudio)*1000)}else{audioFirst=now.timeIntervalSince(start)};lastAudio=now;audioPackets+=1
        data.withUnsafeBytes{raw in
            let bytes=raw.bindMemory(to:UInt8.self).baseAddress
            _=ditto_audio_packet(bytes,data.count)
            if let file=pcmFile{var pcm=[Int16](repeating:0,count:320);let count=ditto_audio_decode(bytes,data.count,&pcm);if count>0{pcm.withUnsafeBytes{file.write(Data($0))}}}
        }
    }
    link.changed={log(["status":$0,"seconds":Date().timeIntervalSince(start)])}
    link.receive={data in
        let now=Date();if packets>0{intervals.append(now.timeIntervalSince(last)*1000)};last=now;packets+=1
    }
    link.acknowledged={seq in acks+=1;if let s=sent{maxACK=max(maxACK,Date().timeIntervalSince(s)*1000)};sent=nil}
    link.start()
    var next=Date(),lease=Date(),sequence:UInt32=0
    while Date().timeIntervalSince(start)<seconds {
        RunLoop.current.run(until:Date().addingTimeInterval(0.02))
        if audioTest && Date()>=lease{lease=Date().addingTimeInterval(0.12);link.send(dictating:true,client:client,event:nil,audio:true)}
        if Date()>=next {
            next=Date().addingTimeInterval(1)
            log(["seconds":Date().timeIntervalSince(start),"packets":packets,"acknowledged":acks,"ready":link.ready,"audio_ready":link.audioReady])
            if !audioTest && link.ready && !link.writing {
                sequence+=1;sent=Date()
                link.send(dictating:false,client:client,event:["kind":"enter","seq":sequence])
            }
        }
    }
    link.send(dictating:false,client:client,event:nil,audio:false)
    link.stop();intervals.sort();audioGaps.sort();pcmFile?.closeFile()
    let audioMax:Double=audioGaps.last ?? 0.0
    log(["audio_packets":audioPackets,"audio_first_s":audioFirst,"audio_gap_p95_ms":audioGaps.isEmpty ? 0:audioGaps[min(audioGaps.count-1,Int(Double(audioGaps.count)*0.95))],"audio_gap_max_ms":audioMax,"audio_lost":ditto_audio_metric(1),"audio_underflows":ditto_audio_metric(2),"audio_overflows":ditto_audio_metric(3),"audio_rendered_samples":ditto_audio_metric(4)])
    ditto_audio_stop()
    let maxGap:Double=intervals.last ?? 0.0
    log(["final":true,"packets":packets,"acks":acks,"max_ack_ms":maxACK,"median_gap_ms":intervals.isEmpty ? 0.0:intervals[intervals.count/2],"p95_gap_ms":intervals.isEmpty ? 0.0:intervals[min(intervals.count-1,Int(Double(intervals.count)*0.95))],"max_gap_ms":maxGap])
    exit(packets>100 && (audioTest ? audioPackets>100:acks>5) ? 0:1)
}
func bluetoothControlSelfTest(){
 let id="12345678-1234-1234-1234-123456789ABC"
 for (kind,code) in [("end",1),("enter",2),("undo",3)] {
  let b=[UInt8](encodeRemoteControl(dictating:true,client:id,event:["seq":UInt32(0x12345678),"kind":kind]))
  precondition(b.count==44 && b[0]==1 && b[1]==1 && b[2]==code && b[3]==0 && Array(b[4..<8])==[0x78,0x56,0x34,0x12] && String(bytes:b[8..<44],encoding:.utf8)==id)
 }
 let idle=[UInt8](encodeRemoteControl(dictating:false,client:id,event:nil));precondition(idle[0]==1 && idle[1..<8].allSatisfy{$0==0})
 let audio=[UInt8](encodeRemoteControl(dictating:true,client:id,event:nil,audio:true));precondition(audio[0]==2&&audio[1]==3&&audio[2]==0)
 print("PASS: BLE control frame, shared retry identity, all action kinds, inactive lease")
}
