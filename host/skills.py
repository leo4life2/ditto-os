"""Provider-independent action definitions for a future language model.

Execution always returns observed device state. The caller must not infer that
wifi.connect means connected: wait for wifi.connected or query system.status.
"""
from avatar_catalog import CLIPS

def schema(name,description,properties=None,required=None):
    return {'type':'function','name':name.replace('.','_'),'description':description,'parameters':{'type':'object','properties':properties or {},'required':required or [],'additionalProperties':False},'device_tool':name}
def integer(lo,hi,description=''):
    return {'type':'integer','minimum':lo,'maximum':hi,'description':description}
def string(description='',**kw):return {'type':'string','description':description,**kw}
SKILLS=[
 schema('system.status','Read actual device state and pending work.'),
 schema('audio.volume.set','Set speaker volume and display the result.',{'percent':integer(0,100)},['percent']),
 schema('display.brightness.set','Set display brightness and display the result.',{'percent':integer(1,100)},['percent']),
 schema('audio.microphone.set','Mute or unmute outgoing microphone audio.',{'muted':{'type':'boolean'}},['muted']),
 schema('wifi.scan','Find nearby Wi-Fi networks. Results arrive as wifi.scan.result.'),
 schema('wifi.draft.set','Prepare a network and its transcribed password for review on screen. This does not connect.',{'ssid':string(maxLength=32),'password':string(maxLength=63)},['ssid','password']),
 schema('wifi.password.edit','Apply one precise correction to the draft password. Index counts bytes starting at zero; use ASCII passwords for this prototype.',{'index':integer(0,63),'delete_count':integer(0,63),'insert':string(maxLength=63)},['index','delete_count','insert']),
 schema('wifi.connect','Attempt the prepared network. Wait for a connection result before reporting success.'),
 schema('wifi.disconnect','Disconnect Wi-Fi while retaining the saved network.'),
 schema('wifi.forget','Disconnect and erase the saved network credentials.'),
 schema('app.open','Open one of the built-in OS surfaces.',{'name':string(enum=['home','volume','brightness','wifi','microphone','timer','system'])},['name']),
 schema('app.home','Return home. An active timer continues running.'),
 schema('timer.start','Start or replace the background timer.',{'seconds':integer(1,86400)},['seconds']),
 schema('timer.cancel','Cancel the background timer.'),
 schema('avatar.emote','Make Scout perform a gesture, then return to the current conversation state.',{'name':string(enum=CLIPS)},['name']),
 schema('avatar.expression','Smoothly change the character expression.',{'name':string(enum=['neutral','curious','sad','angry'])},['name']),
 schema('display.sleep','Ease the screen down to 10% and rest the character. Touch or device movement wakes it.'),
 schema('display.wake','Quickly restore saved brightness, audio and animation.'),
]
BY_NAME={s['name']:s for s in SKILLS}
def execute(device,name,arguments,*,dispatch_guard=None):
    if name not in BY_NAME:raise ValueError('Unknown skill')
    options={'dispatch_guard':dispatch_guard} if dispatch_guard else {}
    return device.call(BY_NAME[name]['device_tool'],arguments,**options)
