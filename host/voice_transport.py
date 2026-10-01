"""Audio and skill boundary for the final cloud-voice integration.

This is a real device transport, not an STT or language-model simulator.
Cloud credentials and the provider adapter are deliberately left to the final step.
"""
import base64,queue,time,threading
from contextlib import contextmanager
from skills import execute
from device import DeviceError

class ResponseCancelled(DeviceError):
    """A stale response was rejected before its command reached USB."""

class VoiceTransport:
    sample_rate=16000
    channels=1
    bytes_per_sample=2
    def __init__(self,device):
        self.device=device;self.input=None;self._stop=threading.Event();self._heartbeat=None
        self._failure=None;self._generation=None
        self._audio_lock=threading.Lock();self._speech_lock=threading.Lock()
        self._speech_id=0;self._audio_open=False
    def _invalidate_speech(self):
        with self._speech_lock:
            self._speech_id+=1
            return self._speech_id
    def _check_session(self):
        if self._failure or self.device.failure:
            raise DeviceError('Voice connection lost; start a new session')
        if self._generation is not None and self._generation != getattr(self.device,'generation',None):
            raise DeviceError('Device reconnected; start a new voice session')
    def _response_token(self,speech_id):
        self._check_session()
        with self._speech_lock:
            if not self._audio_open:raise DeviceError('Audio transport not started')
            return self._speech_id if speech_id is None else speech_id
    @contextmanager
    def _response_guard(self,speech_id):
        # Held only across dispatch, never the device's reply wait. Cancellation
        # wins if it gets here first; an already sent action is not rolled back.
        with self._speech_lock:
            self._check_session()
            if not self._audio_open or speech_id!=self._speech_id:
                raise ResponseCancelled('Response canceled before command dispatch')
            yield
    def start(self):
        if self.input is not None:return
        self._failure=None;self._generation=getattr(self.device,'generation',None)
        self._check_session()
        self.input=self.device.subscribe('audio.input',maxsize=50)
        try:
            self.device.call('audio.capture.start')
            self._generation=getattr(self.device,'generation',None)
            self.set_state('listening','Listening to you')
            with self._speech_lock:
                self._speech_id+=1;self._audio_open=True
        except Exception:
            self.device.unsubscribe(self.input);self.input=None
            raise
        self._stop.clear()
        def heartbeat():
            while not self._stop.wait(2):
                try:
                    self._check_session();self.device.call('voice.keepalive',timeout=3)
                except Exception as e:
                    self._failure=e;break  # Firmware expires the lease and clears speech.
        self._heartbeat=threading.Thread(target=heartbeat,daemon=True)
        self._heartbeat.start()
    def read_audio(self,timeout=.5):
        """Return up to 20 ms of PCM. Muting means no outgoing audio packets."""
        if self.input is None:raise RuntimeError('Audio transport not started')
        self._check_session()
        try:return base64.b64decode(self.input.get(timeout=timeout)['pcm'],validate=True)
        except queue.Empty:return b''
    def write_audio(self,pcm,speech_id=None):
        """Queue PCM; return False when canceled.

        A provider must pass the token returned by begin_speech() on EVERY
        callback for that response, so late chunks cannot restart old speech.
        Omitting it is only for a single local buffer, such as a test tone.
        """
        self._check_session()
        if len(pcm)%2:raise ValueError('PCM must contain complete signed 16-bit samples')
        speech_id=self._response_token(speech_id)
        for start in range(0,len(pcm),640):
            with self._audio_lock:
                self._check_session()
                with self._speech_lock:
                    if not self._audio_open or speech_id!=self._speech_id:return False
                try:
                    self.device.call('audio.play',{'pcm':base64.b64encode(pcm[start:start+640]).decode()},
                                     dispatch_guard=lambda:self._response_guard(speech_id))
                except ResponseCancelled:return False
        return True
    def begin_speech(self):
        """Replace a response and return ownership for its audio/actions/state."""
        return self.interrupt()
    def interrupt(self):
        self._check_session()
        # Stop an in-flight writer before waiting for its current USB command.
        self._invalidate_speech()
        with self._audio_lock:
            self._check_session()
            with self._speech_lock:
                if not self._audio_open:raise DeviceError('Audio transport not started')
            token=self._invalidate_speech()
            self.device.call('audio.clear')
            return token
    def set_state(self,state,caption=None,*,speech_id=None):
        self._check_session()
        args={'state':state}
        if caption is not None:args['caption']=caption
        if speech_id is None:return self.device.call('voice.state.set',args)
        with self._response_guard(speech_id):pass
        return self.device.call('voice.state.set',args,dispatch_guard=lambda:self._response_guard(speech_id))
    def skill(self,name,arguments,*,speech_id=None):
        """Dispatch an action for this response or raise ResponseCancelled.

        Provider callbacks must carry the original begin_speech() token; never
        obtain a fresh token for a late callback. Already dispatched actions
        retain their real result, even if their response is later canceled.
        """
        speech_id=self._response_token(speech_id)
        with self._response_guard(speech_id):pass
        return execute(self.device,name,arguments,dispatch_guard=lambda:self._response_guard(speech_id))
    def close(self):
        self._invalidate_speech()
        with self._speech_lock:self._audio_open=False
        self._stop.set()
        if self._heartbeat:self._heartbeat.join(timeout=3.5)
        try:
            with self._audio_lock:
                if not self.device.failure and self._generation == getattr(self.device,'generation',None):
                    self.device.call('audio.capture.stop');self.device.call('audio.clear')
                    self.device.call('voice.state.set',{'state':'offline','caption':'Voice disconnected'})
        finally:
            if self.input is not None:self.device.unsubscribe(self.input);self.input=None
