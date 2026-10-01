"""Verify the loopback console uses the real device skills and capture path."""
import io, json, time, urllib.request, urllib.error
from pathlib import Path
from PIL import Image
root = Path(__file__).resolve().parents[1]
base = "http://127.0.0.1:8781"
def call(path, body=None, origin=None):
    headers = {"Content-Type": "application/json"}
    if origin: headers["Origin"] = origin
    req = urllib.request.Request(base + path, data=None if body is None else json.dumps(body).encode(), headers=headers)
    with urllib.request.urlopen(req, timeout=30) as r:
        return r.status, r.read()
def skill(name, arguments=None):
    return json.loads(call("/skill", {"name": name, "arguments": arguments or {}})[1])
# HTTP can be ready before the exclusive USB connection has finished booting.
# Poll only read-only status; never retry a settings action of unknown outcome.
for attempt in range(30):
    try:
        call("/status")
        break
    except (urllib.error.URLError, TimeoutError):
        if attempt == 29:
            raise
        time.sleep(.5)
original = skill("system_status")
assert skill("audio_volume_set", {"percent": 37})["volume"] == 37
assert skill("display_brightness_set", {"percent": 62})["brightness"] == 62
skill("audio_volume_set", {"percent": original["volume"]})
skill("display_brightness_set", {"percent": original["brightness"]})
skill("app_home")
call("/capture", {})
image = Image.open(io.BytesIO(call("/frame.png")[1]))
assert image.size == (410, 502)
try:
    call("/skill", {"name": "audio_volume_set", "arguments": {"percent": 99}}, "https://untrusted.invalid")
    raise AssertionError("Unexpected cross-origin action accepted")
except urllib.error.HTTPError as e:
    assert e.code == 403
final = skill("system_status")
assert (final["volume"], final["brightness"]) == (original["volume"], original["brightness"])
assert all(final[k] == 0 for k in ("display_errors", "audio_rx_errors", "audio_tx_errors", "audio_overflows"))
report = {"skills": len(json.loads(call("/skills")[1])), "settings_and_restore": True, "framebuffer_size": list(image.size), "cross_origin_rejected": True, "health": {k: final[k] for k in ("display_errors", "audio_rx_errors", "audio_tx_errors", "audio_overflows", "reset_reason")}}
(root / "test-results/bridge-check.json").write_text(json.dumps(report, indent=2) + "\n")
print(json.dumps(report, indent=2))
