"""Fault-injection check: reconnect without replaying an uncertain mutation."""
import sys,time,queue
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'host'))
from connection import ManagedDevice
from device import DeviceError
instances=[]
class FakeBoard:
    def __init__(self,port):
        self.failure=None;self.closed=False;self.pending={};self.logs=[];self.calls=[];self.subscribers=[];self.events=queue.Queue();instances.append(self)
    def sync_clock(self):self.calls.append('clock.sync')
    def call(self,name,*args,**kwargs):
        self.calls.append(name)
        if name=='timer.start':self.failure=OSError('cable removed');raise DeviceError('completion unknown')
        return {'uptime_s':2}
    def close(self):self.closed=True

def until(fn):
    limit=time.monotonic()+5
    while not fn():
        assert time.monotonic()<limit,'Reconnect timed out';time.sleep(.01)
with_connection=ManagedDevice(device_factory=FakeBoard,poll_interval=.01)
try:
    until(lambda:with_connection.connected)
    old=instances[0]
    try:with_connection.call('timer.start',{'seconds':15});raise AssertionError('Lost reply reported success')
    except DeviceError:pass
    until(lambda:with_connection.connected and with_connection.generation==2)
    assert old.closed
    assert sum(i.calls.count('timer.start') for i in instances)==1,'Mutation replayed'
    assert instances[1].calls==['clock.sync'],'Unexpected restoration of a mutation or audio'
    assert with_connection.call('system.status')['uptime_s']==2
    print('PASS disconnected action fails; automatic reconnect; clock restored; no action/audio replay')
finally:with_connection.close()
