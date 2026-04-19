# netx.py — already present as plain source at netx/netx.py
# Source: E:\PythonProjects\NetX\netx.py
# TM Labs NetX Hook v4.0 — intercepts all auth endpoints

import os
import sys
import requests
import json
import time
import hashlib

_original_post = requests.post
_original_get  = requests.get

YOUR_HWID = "035E02D8-04D3-0582-1606-A10700080009"
LOG_PATH  = r"C:\Users\brael\netx_log.txt"


def get_timestamp():
    return int(time.time())


def _log(msg):
    with open(LOG_PATH, "a") as f:
        f.write(msg + "\n")


def make_database_response(member_id, hwid):
    ts = get_timestamp()
    return {
        "success": True, "status": "active", "result": True,
        "valid": True, "verified": True,
        "member_id": member_id, "id": member_id,
        "hwid": hwid, "hwid_match": True, "hwid_verified": True,
        "hardware_id": hwid, "registered_hwid": hwid, "expected_hwid": hwid,
        "expiry": "2099-12-31T23:59:59", "expiry_timestamp": 4102444799,
        "timestamp": ts, "server_time": ts,
        "cooldown": 0, "cooldown_remaining": 0,
        "reconnect_time": 0, "wait_time": 0, "last_connect": 0,
        "can_connect": True, "connection_allowed": True,
        "rate_limit": False, "banned": False, "active": True,
        "subscription": {"status": "active", "type": "lifetime", "expiry": "2099-12-31"}
    }


def make_netx_response(member_id):
    ts = get_timestamp()
    return {
        "success": True, "result": True,
        "data": {
            "status": "active", "expiry": "2099-12-31",
            "timestamp": ts, "cooldown": 0, "wait_time": 0,
            "reconnect_in": 0, "can_connect": True
        },
        "member_id": member_id, "timestamp": ts,
        "server_timestamp": ts, "current_time": ts,
        "cooldown": 0, "cooldown_seconds": 0,
        "reconnect_time": 0, "wait_minutes": 0,
        "connection_ready": True,
        "nonce": hashlib.md5(str(ts).encode()).hexdigest(),
        "signature": "a" * 64, "verified": True
    }


def make_othr_response(member_id, hwid):
    return {
        "success": True, "status": "active", "valid": True,
        "verified": True, "member_id": member_id,
        "hwid": hwid, "hwid_match": True,
        "files_valid": True, "integrity": True,
        "timestamp": get_timestamp()
    }


class FakeResponse:
    status_code = 200
    ok = True
    headers = {'Content-Type': 'application/json'}

    def __init__(self, data, url=""):
        self.text    = json.dumps(data) if isinstance(data, dict) else str(data)
        self.content = self.text.encode()
        self.url     = url

    def json(self):
        return json.loads(self.text)

    def raise_for_status(self):
        pass


def _fake_post(url, **kwargs):
    _log(f"\n{'='*60}\nPOST: {url}\nKWARGS: {kwargs}")

    if 'discord.com/api/webhooks' in url:
        return FakeResponse({"success": True}, url)

    hwid = YOUR_HWID
    try:
        data = kwargs.get('data', {})
        if isinstance(data, dict) and 'hwid' in data:
            hwid = data['hwid']
    except Exception:
        pass

    if 'auth.inputsense.com/database/' in url:
        try:
            member_id = url.split('/')[-1].replace('.php', '')
        except Exception:
            member_id = "3911"
        resp = make_database_response(member_id, hwid)
        _log(f"FAKED DATABASE: {json.dumps(resp, indent=2)}")
        return FakeResponse(resp, url)

    if 'othr.php' in url:
        try:
            data      = kwargs.get('data', {})
            member_id = data.get('id', '0000000000000000000')
            hwid      = data.get('hwid', YOUR_HWID)
        except Exception:
            member_id = "0000000000000000000"
        resp = make_othr_response(member_id, hwid)
        _log(f"FAKED OTHR: {json.dumps(resp, indent=2)}")
        return FakeResponse(resp, url)

    if '2kv.inputsense.com' in url and ('x7k9q2m' in url or 'p3n8v5x' in url):
        try:
            member_id = kwargs.get('json', {}).get('member_id', '0000000000000000000')
        except Exception:
            member_id = "0000000000000000000"
        resp = make_netx_response(member_id)
        _log(f"FAKED NETX: {json.dumps(resp, indent=2)}")
        return FakeResponse(resp, url)

    if 'auth.inputsense.com' in url:
        resp = {"success": True, "status": "active", "valid": True,
                "verified": True, "cooldown": 0, "timestamp": get_timestamp()}
        _log(f"FAKED GENERIC AUTH: {json.dumps(resp)}")
        return FakeResponse(resp, url)

    response = _original_post(url, **kwargs)
    _log(f"REAL RESPONSE: {response.status_code}")
    try:
        _log(f"BODY: {response.text[:500]}")
    except Exception:
        pass
    return response


def _fake_get(url, **kwargs):
    _log(f"\nGET: {url}")
    if 'discord.com' in url:
        return FakeResponse({"success": True}, url)
    if 'auth.inputsense.com' in url and '/xisutil/' not in url:
        return FakeResponse({"success": True, "status": "active",
                             "cooldown": 0, "timestamp": get_timestamp()}, url)
    return _original_get(url, **kwargs)


# Install hooks
requests.post = _fake_post
requests.get  = _fake_get

print(f"[NETX HOOK] v4.0 — All endpoints hooked")
print(f"[NETX HOOK] HWID: {YOUR_HWID}")

# Launch GUI
from PyQt6.QtWidgets import QApplication
import netx_gui


def main():
    app = QApplication(sys.argv)
    window = netx_gui.GUI()
    window.show()
    sys.exit(app.exec())


if __name__ == "__main__":
    main()
