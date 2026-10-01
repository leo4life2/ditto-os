"""Local development bridge; keeps USB open and automatically syncs Mac time.

HTTP is restricted to loopback and rejects browser requests from other origins.
No cloud calls. Credentials are never logged. Stop this process before flashing.
"""
import argparse,json,threading,time,os
from http.server import BaseHTTPRequestHandler,ThreadingHTTPServer
from pathlib import Path
from device import Device,DeviceError
from connection import ManagedDevice
from skills import SKILLS,execute
from battery import Archive

PAGE=Path(__file__).with_name('console.html').read_text()

def serve(port=8781,device_port=None,device=None):
    owns_device=device is None
    if owns_device:device=ManagedDevice(device_port)
    output=Path(os.environ.get('SCOUT_OUTPUT_DIR',Path(__file__).resolve().parents[1]/'test-results'))
    path=output/'console-frame.png';path.parent.mkdir(parents=True,exist_ok=True)
    operation=threading.Lock();stop=threading.Event()
    battery=Archive(device,stop);threading.Thread(target=battery.run,daemon=True).start()
    class Handler(BaseHTTPRequestHandler):
        def log_message(self,*a):pass
        def send(self,code,data,kind='application/json'):
            if kind=='application/json':data=json.dumps(data).encode()
            elif isinstance(data,str):data=data.encode()
            self.send_response(code);self.send_header('Content-Type',kind);self.send_header('Content-Length',str(len(data)));self.send_header('Cache-Control','no-store');self.end_headers();self.wfile.write(data)
        def do_GET(self):
            try:
                if self.path=='/':return self.send(200,PAGE,'text/html; charset=utf-8')
                if self.path=='/battery':return self.send(200,battery.snapshot())
                if self.path=='/skills':return self.send(200,SKILLS)
                if self.path=='/status':return self.send(200,device.call('system.status'))
                if self.path.startswith('/frame.png') and path.exists():return self.send(200,path.read_bytes(),'image/png')
                self.send(404,{'error':'Not found'})
            except Exception as e:self.send(503,{'error':str(e)})
        def do_POST(self):
            origin=self.headers.get('Origin');allowed=(None,f'http://127.0.0.1:{port}',f'http://localhost:{port}')
            if origin not in allowed:return self.send(403,{'error':'Origin not allowed'})
            try:
                n=int(self.headers.get('Content-Length','0'))
                if n>4096:return self.send(413,{'error':'Request too large'})
                args=json.loads(self.rfile.read(n) or b'{}')
                with operation:
                    if self.path=='/capture':device.screenshot(path);result={'captured':True}
                    elif self.path=='/skill':result=execute(device,args['name'],args.get('arguments',{}))
                    elif self.path=='/action':result=device.call(args['tool'],args.get('arguments',{}))
                    else:return self.send(404,{'error':'Not found'})
                self.send(200,result)
            except DeviceError as e:self.send(503 if not device.connected else 400,{'error':str(e)})
            except (ValueError,KeyError,TypeError) as e:self.send(400,{'error':str(e)})
    server=ThreadingHTTPServer(('127.0.0.1',port),Handler)
    print(f'Scout bridge ready at http://127.0.0.1:{port}',flush=True)
    try:server.serve_forever()
    finally:
        stop.set();server.server_close()
        if owns_device:device.close()
if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--port',type=int,default=8781);p.add_argument('--device-port');a=p.parse_args();serve(a.port,a.device_port)
