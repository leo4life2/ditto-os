"""Scout USB transport. The future AI and the test runner use this exact API.

Only the Espressif USB Serial/JTAG device is eligible for auto-discovery.
Requests and credentials are never written to logs.
"""
import argparse, base64, datetime, json, queue, threading, time, uuid
from contextlib import nullcontext
from pathlib import Path
import serial
from serial.tools import list_ports

class DeviceError(RuntimeError): pass

class Device:
    def __init__(self, port=None):
        if port is None:
            ports=[p.device for p in list_ports.comports() if (p.vid,p.pid) in {(0x303a,0x1001),(0x303a,0x4001)}]
            if len(ports)!=1: raise DeviceError(f'Expected one Espressif board, found {len(ports)}')
            port=ports[0]
        # POSIX advisory ownership prevents two Scout readers consuming the
        # same replies. Tools that do not request a lock still need coordination.
        self.serial=serial.Serial(port=None,baudrate=921600,timeout=.1,write_timeout=2,exclusive=True)
        self.serial.dtr=False; self.serial.rts=False; self.serial.port=port; self.serial.open()
        if any(p.device==port and (p.vid,p.pid)==(0x303a,0x4001) for p in list_ports.comports()):
            self.serial.dtr=True # CDC needs a ready host; ROM Serial/JTAG must not reset.
        self.pending={};self.lock=threading.Lock();self.events=queue.Queue(maxsize=2048)
        self.captures={};self.capture_rows={};self.logs=[];self.closed=False;self.failure=None;self.boot_count=0
        self.subscribers=[]
        self.thread=threading.Thread(target=self._read,daemon=True);self.thread.start()

    def _read(self):
        buf=bytearray()
        try:
            while not self.closed:
                data=self.serial.read(self.serial.in_waiting or 1)
                if not data: continue
                buf.extend(data)
                while b'\n' in buf:
                    raw,_,buf=buf.partition(b'\n')
                    marker=raw.find(b'SCOUT ')
                    if marker<0:
                        line=raw.decode(errors='replace').strip()
                        if line:self.logs.append(line);self.logs=self.logs[-100:]
                        continue
                    try: ev=json.loads(raw[marker+6:])
                    except (ValueError,UnicodeError):continue
                    if ev.get('event')=='system.ready':self.boot_count+=1
                    rid=ev.get('id')
                    if ev.get('event')=='display.capture' and rid in self.captures:
                        pixels=base64.b64decode(ev['pixels'],validate=True)
                        y,rows=ev['y'],ev['rows']
                        if not isinstance(y,int) or not isinstance(rows,int) or y<0 or rows<=0 or y+rows>502 or len(pixels)!=rows*410*2:
                            raise DeviceError('Malformed display capture')
                        offset=y*410*2
                        self.captures[rid][offset:offset+len(pixels)]=pixels
                        self.capture_rows[rid].update(range(y,y+rows))
                    elif 'ok' in ev and rid in self.pending:self.pending[rid].put(ev)
                    else:
                        for name,sub in list(self.subscribers):
                            if name is None or ev.get('event')==name:
                                try:sub.put_nowait(ev)
                                except queue.Full:
                                    try:sub.get_nowait()
                                    except queue.Empty:pass
                                    try:sub.put_nowait(ev)
                                    except queue.Full:pass
                        try:self.events.put_nowait(ev)
                        except queue.Full: self.events.get_nowait();self.events.put_nowait(ev)
                if len(buf)>65536:buf.clear()
        except Exception as e:
            if not self.closed:
                self.failure=e
                for reply in list(self.pending.values()):
                    reply.put({'ok':False,'error':'USB connection lost; action completion is unknown'})

    def call(self, tool, arguments=None, timeout=8, request_id=None, dispatch_guard=None):
        """Send once; an optional context manager guards only the USB write.

        Its factory runs AFTER waiting for the serial lock. A canceled response
        can therefore withdraw a queued command, without holding its ownership
        lock while waiting for a potentially slow device reply.
        """
        if self.closed or self.failure:raise DeviceError('USB device is disconnected')
        rid=request_id or uuid.uuid4().hex;reply=queue.Queue();self.pending[rid]=reply
        request={'id':rid,'tool':tool,'arguments':arguments or {}}
        try:
            packet=(json.dumps(request,separators=(',',':'))+'\n').encode()
            try:
                with self.lock:
                    with dispatch_guard() if dispatch_guard else nullcontext():
                        if self.closed or self.failure:raise DeviceError('USB device is disconnected')
                        self.serial.write(packet)
            except (serial.SerialException,OSError) as e:
                self.failure=e
                raise DeviceError('USB write failed; action completion is unknown') from e
            try:result=reply.get(timeout=timeout)
            except queue.Empty:raise DeviceError(f'{tool} timed out; recent device log: {self.logs[-3:]}')
            if not result['ok']:raise DeviceError(result.get('error','Device rejected action'))
            return result.get('result')
        finally:self.pending.pop(rid,None)

    def wait_event(self, name, timeout=10):
        deadline=time.monotonic()+timeout
        while time.monotonic()<deadline:
            try:ev=self.events.get(timeout=max(.01,deadline-time.monotonic()))
            except queue.Empty:break
            if ev.get('event')==name:return ev
        raise DeviceError(f'No {name} event')

    def subscribe(self,name=None,maxsize=256):
        q=queue.Queue(maxsize=maxsize);self.subscribers.append((name,q));return q
    @property
    def generation(self):return self.boot_count
    def unsubscribe(self,q):self.subscribers[:]=[(n,s) for n,s in self.subscribers if s is not q]

    def screenshot(self, path):
        from PIL import Image
        rid=uuid.uuid4().hex;self.captures[rid]=bytearray(410*502*2);self.capture_rows[rid]=set()
        try:
            self.call('system.screenshot',timeout=20,request_id=rid)
            if len(self.capture_rows[rid])!=502:raise DeviceError('Incomplete display capture; retry')
            raw=self.captures[rid];rgb=bytearray()
            for i in range(0,len(raw),2):
                p=raw[i]|raw[i+1]<<8;rgb.extend((((p>>11)&31)*255//31,((p>>5)&63)*255//63,(p&31)*255//31))
            image=Image.frombytes('RGB',(410,502),bytes(rgb));image.save(path);return image
        finally:self.captures.pop(rid,None);self.capture_rows.pop(rid,None)

    def sync_clock(self):
        offset=int(datetime.datetime.now().astimezone().utcoffset().total_seconds())
        sign='-' if offset>=0 else '+';h,m=divmod(abs(offset)//60,60)
        return self.call('clock.sync',{'epoch':time.time(),'timezone':f'UTC{sign}{h}:{m:02d}'})

    def close(self):
        self.closed=True;self.thread.join(timeout=.3);self.serial.close()
    def __enter__(self):return self
    def __exit__(self,*args):self.close()

def main():
    p=argparse.ArgumentParser();p.add_argument('--port');p.add_argument('tool',nargs='?',default='system.status');p.add_argument('arguments',nargs='?',default='{}');p.add_argument('--screenshot');args=p.parse_args()
    with Device(args.port) as d:
        if args.screenshot:d.screenshot(args.screenshot);print(args.screenshot)
        elif args.tool=='clock.sync':print(json.dumps(d.sync_clock(),indent=2))
        else:print(json.dumps(d.call(args.tool,json.loads(args.arguments)),indent=2))
if __name__=='__main__':main()
