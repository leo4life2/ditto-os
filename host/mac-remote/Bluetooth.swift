import Cocoa
import CoreBluetooth

func decodeRemotePacket(_ data:Data,after:UInt32)->[String:Any]? {
    let b=[UInt8](data)
    guard b.count>=28,b[15]==1,b[13]<=4,b.count==28+Int(b[13])*16 else{return nil}
    func u(_ offset:Int,_ size:Int)->UInt64 {var v:UInt64=0;for i in 0..<size{v |= UInt64(b[offset+i]) << (8*i)};return v}
    func signed(_ offset:Int)->Double {Double(Int16(bitPattern:UInt16(u(offset,2))))}
    let us=Double(u(0,8)),seq=UInt32(u(8,4)),flags=b[12]
    var edges:[[Double]]=[]
    for i in 0..<Int(b[13]) {let o=28+i*16,f=b[o+12];edges.append([Double(u(o,4)),max(0,us-Double(u(o+4,4))),signed(o+8),signed(o+10),f&1 != 0 ? 1:0,f&2 != 0 ? 1:0,f&4 != 0 ? 1:0])}
    return ["us":us,"seq":NSNumber(value:seq),"down":flags&1 != 0,"sleeping":flags&2 != 0,"simulated":flags&4 != 0,"motion_valid":flags&8 != 0,"x":signed(16),"y":signed(18),"linear":[signed(20)/4096,signed(22)/4096,signed(24)/4096],"edges":edges,"overflow":seq>=after && seq-after>4,"battery":b[14]==255 ? -1:Int(b[14])]
}
func encodeRemoteControl(dictating:Bool,client:String,event:[String:Any]?,audio:Bool=false)->Data {
    var b=[UInt8](repeating:0,count:44);b[0]=audio ? 2:1;b[1]=(dictating ? 1:0)|(audio ? 2:0)
    if let e=event,let k=e["kind"] as? String,let q=e["seq"] as? UInt32 {
        b[2]=k=="end" ? 1:k=="enter" ? 2:3
        for i in 0..<4{b[4+i]=UInt8(truncatingIfNeeded:q >> (8*i))}
    }
    let id=Array(client.utf8);precondition(id.count==36)
    b.replaceSubrange(8..<44,with:id);return Data(b)
}
final class BluetoothLink:NSObject,CBCentralManagerDelegate,CBPeripheralDelegate {
    static let service=CBUUID(string:"D1D70000-A76B-47F1-A982-F194A1334701")
    static let input=CBUUID(string:"D1D70001-A76B-47F1-A982-F194A1334701")
    static let control=CBUUID(string:"D1D70002-A76B-47F1-A982-F194A1334701")
    static let audio=CBUUID(string:"D1D70003-A76B-47F1-A982-F194A1334701")
    var receivedAudio:((Data)->Void)?,audioReady=false,audioCharacteristic:CBCharacteristic?,lastAudioSubscribe=Date.distantPast
    var protectedInput=false,audioSubscribePending=false
    var central:CBCentralManager?,peripheral:CBPeripheral?,characteristic:CBCharacteristic?,controlCharacteristic:CBCharacteristic?
    var receive:((Data)->Void)?,changed:((String)->Void)?,acknowledged:((UInt32)->Void)?
    var wanted=false,ready=false,savedAttempted=false,writing=false
    var pendingSequence:UInt32=0,lastWrite=Date.distantPast,lastPacket=Date.distantPast
    var deadline:DispatchWorkItem?,watchdog:Timer?
    func diagnostic(_ text:String){if CommandLine.arguments.contains("--bluetooth-probe"){changed?(text)}}
    func start(){
        wanted=true;savedAttempted=false
        changed?(CBCentralManager.authorization == .notDetermined ? "Bluetooth permission needed":"Connecting Bluetooth…")
        if watchdog==nil {watchdog=Timer(timeInterval:0.15,repeats:true){[weak self] _ in
            guard let s=self,s.wanted,let p=s.peripheral,p.state == .connected else{return}
            if (s.ready && Date().timeIntervalSince(s.lastPacket)>1.5) || (s.writing && Date().timeIntervalSince(s.lastWrite)>0.8){s.diagnostic("Watchdog: packet age \(Date().timeIntervalSince(s.lastPacket)), write pending \(s.writing)");s.central?.cancelPeripheralConnection(p)}
        };RunLoop.main.add(watchdog!,forMode:.common)}
        if let c=central{connect(c)}else{central=CBCentralManager(delegate:self,queue:.main)}
    }
    func stop(){wanted=false;deadline?.cancel();watchdog?.invalidate();watchdog=nil;central?.stopScan();reset();if let p=peripheral{central?.cancelPeripheralConnection(p)};changed?("Bluetooth off")}
    func reset(){audioReady=false;audioCharacteristic=nil;lastAudioSubscribe = .distantPast;protectedInput=false;audioSubscribePending=false;ready=false;writing=false;pendingSequence=0;characteristic=nil;controlCharacteristic=nil;lastPacket = .distantPast}
    func centralManagerDidUpdateState(_ central:CBCentralManager){
        switch central.state {case .poweredOn:if wanted{connect(central)}
        case .unauthorized:reset();changed?("Bluetooth permission needed")
        case .poweredOff:reset();changed?("Bluetooth is off")
        default:reset();changed?("Bluetooth unavailable")}
    }
    func connect(_ c:CBCentralManager){
        guard wanted,c.state == .poweredOn else{return}
        if let p=peripheral,p.state == .connected || p.state == .connecting{return}
        if let p=c.retrieveConnectedPeripherals(withServices:[Self.service]).first{begin(p);return}
        if !savedAttempted,let id=UserDefaults.standard.string(forKey:"dittoPeripheral"),let uuid=UUID(uuidString:id),let saved=c.retrievePeripherals(withIdentifiers:[uuid]).first {
            savedAttempted=true;begin(saved)
        }else {changed?("Looking for Ditto…");c.scanForPeripherals(withServices:[Self.service])}
    }
    func armDeadline(_ p:CBPeripheral,seconds:Double){
        deadline?.cancel();let job=DispatchWorkItem{[weak self,weak p] in
            guard let s=self,let p=p,s.wanted,!s.ready,s.peripheral===p else{return}
            s.diagnostic("Bluetooth setup timed out; reconnecting");s.central?.cancelPeripheralConnection(p)
        };deadline=job;DispatchQueue.main.asyncAfter(deadline:.now()+seconds,execute:job)
    }
    func begin(_ p:CBPeripheral){
        central?.stopScan();reset();peripheral=p;p.delegate=self;central?.connect(p);changed?("Connecting Bluetooth…")
        armDeadline(p,seconds:12)
    }
    func centralManager(_ central:CBCentralManager,didDiscover peripheral:CBPeripheral,advertisementData:[String:Any],rssi:NSNumber){if wanted && self.peripheral?.state != .connecting && self.peripheral?.state != .connected{begin(peripheral)}}
    func centralManager(_ central:CBCentralManager,didConnect peripheral:CBPeripheral){
        guard wanted else{central.cancelPeripheralConnection(peripheral);return}
        peripheral.discoverServices([Self.service]);changed?("Pairing with Ditto…")
        // Give a first pairing time for the macOS approval dialog, but do not
        // leave a stalled service discovery hanging forever after reconnect.
        let known=UserDefaults.standard.string(forKey:"dittoPeripheral")==peripheral.identifier.uuidString
        armDeadline(peripheral,seconds:known ? 15:120)
    }
    func centralManager(_ central:CBCentralManager,didFailToConnect peripheral:CBPeripheral,error:Error?){reset();retry()}
    func centralManager(_ central:CBCentralManager,didDisconnectPeripheral peripheral:CBPeripheral,error:Error?){reset();if wanted{changed?("Bluetooth disconnected");retry()}}
    func retry(){DispatchQueue.main.asyncAfter(deadline:.now()+2){[weak self] in if let s=self,let c=s.central,s.wanted{s.connect(c)}}}
    func fail(){if let p=peripheral{central?.cancelPeripheralConnection(p)}}
    func peripheral(_ peripheral:CBPeripheral,didModifyServices invalidatedServices:[CBService]){diagnostic("Services changed; rediscovering");reset();armDeadline(peripheral,seconds:15);peripheral.discoverServices([Self.service])}
    func peripheral(_ peripheral:CBPeripheral,didDiscoverServices error:Error?){
        guard error==nil,let service=peripheral.services?.first(where:{$0.uuid==Self.service}) else{fail();return}
        peripheral.discoverCharacteristics([Self.input,Self.control,Self.audio],for:service)
    }
    func peripheral(_ peripheral:CBPeripheral,didDiscoverCharacteristicsFor service:CBService,error:Error?){
        guard error==nil else{fail();return}
        guard characteristic==nil else{return} // Duplicate discovery after Service Changed.
        diagnostic("Characteristics: \(service.characteristics?.map{$0.uuid.uuidString} ?? [])")
        characteristic=service.characteristics?.first(where:{$0.uuid==Self.input})
        controlCharacteristic=service.characteristics?.first(where:{$0.uuid==Self.control})
        guard let ch=characteristic,controlCharacteristic != nil else{changed?("Ditto update needed");fail();return}
        audioCharacteristic=service.characteristics?.first(where:{$0.uuid==Self.audio})
        peripheral.setNotifyValue(true,for:ch)
    }
    func advanceSetup(){
        guard wanted,protectedInput,characteristic?.isNotifying==true,let p=peripheral else{return}
        if let mic=audioCharacteristic,!audioReady {
            if !audioSubscribePending,Date().timeIntervalSince(lastAudioSubscribe)>1 {
                audioSubscribePending=true;lastAudioSubscribe=Date();p.setNotifyValue(true,for:mic)
            }
            return
        }
        if !ready {ready=true;deadline?.cancel();UserDefaults.standard.set(p.identifier.uuidString,forKey:"dittoPeripheral");changed?("Bluetooth ready")}
    }
    func peripheral(_ peripheral:CBPeripheral,didUpdateNotificationStateFor characteristic:CBCharacteristic,error:Error?){
        diagnostic("Notify \(characteristic.uuid): \(characteristic.isNotifying), \(String(describing:error))")
        if characteristic.uuid==Self.audio {
            audioSubscribePending=false;audioReady=error==nil && characteristic.isNotifying;advanceSetup()
        } else if characteristic.uuid==Self.input {
            if error != nil || !characteristic.isNotifying {fail();return}
            // Firmware requires encryption on the CCC write. Wait for its
            // protected notifications instead of queuing a redundant read
            // while macOS is completing the encryption transition.
            advanceSetup()
        }
    }
    func peripheral(_ peripheral:CBPeripheral,didUpdateValueFor characteristic:CBCharacteristic,error:Error?){
        if let error=error {changed?("Bluetooth: \(error.localizedDescription)");return}
        if wanted,characteristic.uuid==Self.audio,let d=characteristic.value{receivedAudio?(d);return}
        guard wanted,characteristic.uuid==Self.input,let d=characteristic.value,decodeRemotePacket(d,after:0) != nil else{return}
        lastPacket=Date()
        protectedInput=true;advanceSetup()
        if ready{receive?(d)}
    }
    func send(dictating:Bool,client:String,event:[String:Any]?,audio:Bool=false){
        guard ready,!writing,Date().timeIntervalSince(lastWrite)>=0.1,let p=peripheral,let ch=controlCharacteristic else{return}
        pendingSequence=event?["seq"] as? UInt32 ?? 0;writing=true;lastWrite=Date()
        p.writeValue(encodeRemoteControl(dictating:dictating,client:client,event:event,audio:audio),for:ch,type:.withResponse)
    }
    func peripheral(_ peripheral:CBPeripheral,didWriteValueFor characteristic:CBCharacteristic,error:Error?){
        guard characteristic.uuid==Self.control,writing else{return}
        let seq=pendingSequence;writing=false;pendingSequence=0
        if error==nil {if seq>0{acknowledged?(seq)}}else{diagnostic("Write failed: \(String(describing:error))");fail()}
    }
}
