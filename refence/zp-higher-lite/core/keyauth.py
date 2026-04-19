"""
keyauth.py - License authentication via KeyAuth service
Reconstructed from decompiled keyauth module in ui.dll
Replace the placeholders below with your own KeyAuth app credentials.
"""
import sys
import requests
import hashlib
import json
import os


class KeyAuth:
    # ── Replace with your KeyAuth app details ────────────────────────────────
    APP_NAME    = "ZP HIGHER Lite"
    OWNER_ID    = "YOUR_OWNER_ID"       # from keyauth.win dashboard
    APP_SECRET  = "YOUR_APP_SECRET"
    APP_VERSION = "1.0"
    API_URL     = "https://keyauth.win/api/1.2/"

    def __init__(self):
        self._session_token = None
        self._hwid = self._get_hwid()

    def _get_hwid(self) -> str:
        """Generate a hardware ID from system info."""
        try:
            import winreg
            reg = winreg.OpenKey(
                winreg.HKEY_LOCAL_MACHINE,
                r"SOFTWARE\Microsoft\Cryptography"
            )
            machine_guid, _ = winreg.QueryValueEx(reg, "MachineGuid")
            return hashlib.sha256(machine_guid.encode()).hexdigest()
        except Exception:
            import uuid
            return str(uuid.getnode())

    def authenticate(self) -> bool:
        """
        Show login dialog and authenticate with KeyAuth.
        Returns True if authentication succeeds.
        """
        # Try to load saved session
        if self._load_session():
            return True

        # Prompt for credentials
        key = self._prompt_key()
        if not key:
            return False

        return self._redeem_key(key)

    def _prompt_key(self) -> str:
        """Show a simple console prompt for the license key."""
        try:
            from PySide6.QtWidgets import QApplication, QInputDialog
            app = QApplication.instance() or QApplication(sys.argv)
            key, ok = QInputDialog.getText(
                None,
                "ZP HIGHER Lite — Activation",
                "Enter your license key:"
            )
            return key.strip() if ok else ""
        except Exception:
            return input("Enter license key: ").strip()

    def _redeem_key(self, key: str) -> bool:
        try:
            resp = requests.post(self.API_URL, data={
                "type":    "license",
                "key":     key,
                "hwid":    self._hwid,
                "name":    self.APP_NAME,
                "ownerid": self.OWNER_ID,
                "secret":  self.APP_SECRET,
                "ver":     self.APP_VERSION,
            }, timeout=10)
            data = resp.json()
            if data.get("success"):
                self._session_token = data.get("sessionid")
                self._save_session(self._session_token)
                return True
            print(f"[!] Auth failed: {data.get('message', 'Unknown error')}")
            return False
        except Exception as e:
            print(f"[!] Auth error: {e}")
            return False

    def _save_session(self, token: str):
        path = os.path.join(os.getenv("APPDATA"), "ZP_HIGHER", "session.dat")
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, "w") as f:
            json.dump({"token": token, "hwid": self._hwid}, f)

    def _load_session(self) -> bool:
        path = os.path.join(os.getenv("APPDATA"), "ZP_HIGHER", "session.dat")
        if not os.path.exists(path):
            return False
        try:
            with open(path) as f:
                data = json.load(f)
            if data.get("hwid") != self._hwid:
                return False
            self._session_token = data.get("token")
            return bool(self._session_token)
        except Exception:
            return False
