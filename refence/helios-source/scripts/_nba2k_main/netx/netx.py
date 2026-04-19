"""
TM Labs NetX Hook v4.0
Handles ALL auth endpoints with proper responses
"""

import os
import sys
import requests
import json
import time
import hashlib

_original_post = requests.post
_original_get = requests.get

# Your actual HWID from the logs
YOUR_HWID = "035E02D8-04D3-0582-1606-A10700080009"

def get_timestamp():
    return int(time.time())

def make_database_response(member_id, hwid):
    """Response for /database/XXXX.php endpoints"""
    return {
        "success": True,
        "status": "active",
        "result": True,
        "valid": True,
        "verified": True,
        "member_id": member_id,
        "id": member_id,
        "hwid": hwid,
        "hwid_match": True,
        "hwid_verified": True,
        "hardware_id": hwid,
        "registered_hwid": hwid,
        "expected_hwid": hwid,
        "expiry": "2099-12-31T23:59:59",
        "expiry_timestamp": 4102444799,
        "timestamp": get_timestamp(),
        "server_time": get_timestamp(),
        "cooldown": 0,
        "cooldown_remaining": 0,
        "reconnect_time": 0,
        "wait_time": 0,
        "last_connect": 0,
        "can_connect": True,
        "connection_allowed": True,
        "rate_limit": False,
        "banned": False,
        "active": True,
        "subscription": {
            "status": "active",
            "type": "lifetime",
            "expiry": "2099-12-31"
        }
    }

def make_netx_response(member_id):
    """Response for x7k9q2m.php and p3n8v5x.php"""
    ts = get_timestamp()
    return {
        "success": True,
        "result": True,
        "data": {
            "status": "active",
            "expiry": "2099-12-31",
            "timestamp": ts,
            "cooldown": 0,
            "wait_time": 0,
            "reconnect_in": 0,
            "can_connect": True
        },
        "member_id": member_id,
        "timestamp": ts,
        "server_timestamp": ts,
        "current_time": ts,
        "cooldown": 0,
        "cooldown_seconds": 0,
        "reconnect_time": 0,
        "wait_minutes": 0,
        "connection_ready": True,
        "nonce": hashlib.md5(str(ts).encode()).hexdigest(),
        "signature": "a" * 64,
        "verified": True
    }

def make_othr_response(member_id, hwid):
    """Response for othr.php endpoint"""
    return {
        "success": True,
        "status": "active", 
        "valid": True,
        "verified": True,
        "member_id": member_id,
        "hwid": hwid,
        "hwid_match": True,
        "files_valid": True,
        "integrity": True,
        "timestamp": get_timestamp()
    }

class FakeResponse:
    status_code = 200
    ok = True
    headers = {'Content-Type': 'application/json'}
    
    def __init__(self, data, url=""):
        if isinstance(data, dict):
            self.text = json.dumps(data)
        else:
            self.text = str(data)
        self.content = self.text.encode()
        self.url = url
    
    def json(self):
        return json.loads(self.text)
    
    def raise_for_status(self):
        pass

def _fake_post(url, **kwargs):
    # Log everything
    with open("C:\\Users\\brael\\netx_log.txt", "a") as f:
        f.write(f"\n{'='*60}\nPOST: {url}\n")
        f.write(f"KWARGS: {kwargs}\n")
    
    # Block Discord webhooks
    if 'discord.com/api/webhooks' in url:
        return FakeResponse({"success": True}, url)
    
    # Extract HWID from request if present
    hwid = YOUR_HWID
    try:
        data = kwargs.get('data', {})
        if isinstance(data, dict) and 'hwid' in data:
            hwid = data['hwid']
    except:
        pass
    
    # Handle /database/XXXX.php (like 3911.php)
    if 'auth.inputsense.com/database/' in url:
        # Extract member_id from URL (e.g., "3911" from "3911.php")
        try:
            filename = url.split('/')[-1]
            member_id = filename.replace('.php', '')
        except:
            member_id = "3911"
        
        response = make_database_response(member_id, hwid)
        
        with open("C:\\Users\\brael\\netx_log.txt", "a") as f:
            f.write(f"FAKED DATABASE RESPONSE: {json.dumps(response, indent=2)}\n")
        
        return FakeResponse(response, url)
    
    # Handle othr.php
    if 'othr.php' in url:
        try:
            data = kwargs.get('data', {})
            member_id = data.get('id', '0000000000000000000')
            hwid = data.get('hwid', YOUR_HWID)
        except:
            member_id = "0000000000000000000"
        
        response = make_othr_response(member_id, hwid)
        
        with open("C:\\Users\\brael\\netx_log.txt", "a") as f:
            f.write(f"FAKED OTHR RESPONSE: {json.dumps(response, indent=2)}\n")
        
        return FakeResponse(response, url)
    
    # Handle NetX endpoints (x7k9q2m.php, p3n8v5x.php)
    if '2kv.inputsense.com' in url and ('x7k9q2m' in url or 'p3n8v5x' in url):
        try:
            req_json = kwargs.get('json', {})
            member_id = req_json.get('member_id', '0000000000000000000')
        except:
            member_id = "0000000000000000000"
        
        response = make_netx_response(member_id)
        
        with open("C:\\Users\\brael\\netx_log.txt", "a") as f:
            f.write(f"FAKED NETX RESPONSE: {json.dumps(response, indent=2)}\n")
        
        return FakeResponse(response, url)
    
    # Handle any other auth.inputsense.com requests
    if 'auth.inputsense.com' in url:
        response = {
            "success": True,
            "status": "active",
            "valid": True,
            "verified": True,
            "cooldown": 0,
            "timestamp": get_timestamp()
        }
        
        with open("C:\\Users\\brael\\netx_log.txt", "a") as f:
            f.write(f"FAKED GENERIC AUTH: {json.dumps(response)}\n")
        
        return FakeResponse(response, url)
    
    # Let other requests through
    response = _original_post(url, **kwargs)
    
    with open("C:\\Users\\brael\\netx_log.txt", "a") as f:
        f.write(f"REAL RESPONSE: {response.status_code}\n")
        try:
            f.write(f"BODY: {response.text[:500]}\n")
        except:
            pass
    
    return response

def _fake_get(url, **kwargs):
    with open("C:\\Users\\brael\\netx_log.txt", "a") as f:
        f.write(f"\nGET: {url}\n")
    
    if 'discord.com' in url:
        return FakeResponse({"success": True}, url)
    
    # Let hash checks through (they need real hashes for file validation)
    # But intercept any auth-related GETs
    if 'auth.inputsense.com' in url and '/xisutil/' not in url:
        response = {
            "success": True,
            "status": "active",
            "cooldown": 0,
            "timestamp": get_timestamp()
        }
        return FakeResponse(response, url)
    
    return _original_get(url, **kwargs)

# Install hooks
requests.post = _fake_post
requests.get = _fake_get

print("[NETX HOOK] v4.0 - All endpoints hooked")
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