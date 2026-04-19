"""
network_optimizer.py - Network stack optimisation for low latency
Reconstructed from decompiled network_optimizer module in ui.dll
RESTORE_NETWORK.bat (in assets/) reverses these changes.
"""
import subprocess
import os


class NetworkOptimizer:
    # Registry tweaks applied (HKLM\SYSTEM\CurrentControlSet\Services\Tcpip\Parameters)
    TWEAKS = {
        "TcpAckFrequency":         "1",
        "TCPNoDelay":              "1",
        "TcpDelAckTicks":          "0",
        "GlobalMaxTcpWindowSize":  "65535",
        "TcpWindowSize":           "65535",
    }

    def apply(self):
        """Apply low-latency network tweaks via netsh / registry."""
        try:
            # Disable Nagle's algorithm
            self._set_registry_values()
            # Disable Network Throttling Index
            subprocess.run(
                ["netsh", "int", "tcp", "set", "global", "autotuninglevel=disabled"],
                capture_output=True, timeout=5
            )
        except Exception as e:
            pass  # Non-fatal

    def restore(self):
        """Restore original network settings."""
        bat = os.path.join(os.path.dirname(__file__), "..", "assets", "RESTORE_NETWORK.bat")
        if os.path.exists(bat):
            try:
                subprocess.run([bat], capture_output=True, timeout=10)
            except Exception:
                pass

    def _set_registry_values(self):
        try:
            import winreg
            key_path = r"SYSTEM\CurrentControlSet\Services\Tcpip\Parameters"
            key = winreg.OpenKey(
                winreg.HKEY_LOCAL_MACHINE, key_path,
                0, winreg.KEY_SET_VALUE
            )
            for name, value in self.TWEAKS.items():
                winreg.SetValueEx(key, name, 0, winreg.REG_DWORD, int(value))
            winreg.CloseKey(key)
        except Exception:
            pass
