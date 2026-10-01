"""Reconnect the local board without replaying uncertain actions or speech."""
import queue,threading,time
from device import Device,DeviceError

class ManagedDevice:
    def __init__(self,port=None,device_factory=Device,poll_interval=.5):
        self.port=port;self.factory=device_factory;self.poll_interval=poll_interval
        self._device=None;self._boot_count=0;self._lock=threading.Lock();self._stop=threading.Event()
        self.events=queue.Queue(maxsize=2048);self.subscribers=[];self.generation=0
        self._thread=threading.Thread(target=self._run,daemon=True);self._thread.start()
    @property
    def connected(self):
        d=self._device
        return d is not None and not d.failure and not d.closed and getattr(d,'boot_count',0)==self._boot_count
    @property
    def failure(self):return None if self.connected else DeviceError('Device disconnected')
    @property
    def logs(self):return list(self._device.logs) if self._device else []
    def _get(self):
        with self._lock:d=self._device
        if d is None or d.failure or d.closed:raise DeviceError('Device disconnected; reconnecting automatically')
        if getattr(d,'boot_count',0)!=self._boot_count:raise DeviceError('Device rebooted; reconnecting automatically')
        return d
    def _drop(self,d):
        with self._lock:
            if self._device is d:self._device=None
        d.close()
        for _,q in list(self.subscribers):
            while True:
                try:q.get_nowait()
                except queue.Empty:break
    def _run(self):
        last_health=0;last_clock=0;uptime=None
        while not self._stop.is_set():
            d=self._device
            try:
                if d is None:
                    d=self.factory(self.port)
                    if self._stop.wait(1):d.close();break
                    initial=d.sync_clock()
                    d.events=self.events;d.subscribers=self.subscribers
                    with self._lock:
                        self._device=d;self._boot_count=getattr(d,'boot_count',0);self.generation+=1
                    last_health=last_clock=time.monotonic();uptime=initial.get('uptime_s') if isinstance(initial,dict) else None
                elif d.failure or d.closed or getattr(d,'boot_count',0)!=self._boot_count:self._drop(d)
                elif not d.pending and time.monotonic()-last_health>=5:
                    s=d.call('system.status',timeout=3)
                    if uptime is not None and s['uptime_s']<uptime:
                        # Native USB can stay enumerated through a software
                        # reboot. End the old generation just as for unplugging.
                        self._drop(d);continue
                    if time.monotonic()-last_clock>=60:
                        d.sync_clock();last_clock=time.monotonic()
                    uptime=s['uptime_s'];last_health=time.monotonic()
            except Exception:
                if d is not None:self._drop(d)
            self._stop.wait(self.poll_interval)
    def call(self,*args,**kwargs):
        # An interrupted mutation is never resent: its result may be unknown.
        return self._get().call(*args,**kwargs)
    def screenshot(self,*args,**kwargs):return self._get().screenshot(*args,**kwargs)
    def sync_clock(self):return self._get().sync_clock()
    def subscribe(self,name=None,maxsize=256):
        q=queue.Queue(maxsize=maxsize);self.subscribers.append((name,q));return q
    def unsubscribe(self,q):self.subscribers[:]=[(n,s) for n,s in self.subscribers if s is not q]
    def wait_event(self,*args,**kwargs):return Device.wait_event(self,*args,**kwargs)
    def close(self):
        self._stop.set();self._thread.join(timeout=10)
        if self._device:self._drop(self._device)
