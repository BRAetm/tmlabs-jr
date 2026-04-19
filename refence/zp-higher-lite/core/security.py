"""
security.py - Process integrity and anti-tamper checks
Reconstructed from decompiled security module in ui.dll
"""
import os
import sys
import psutil


class Security:
    # Known analysis/debugging tools to check for
    BLOCKED_PROCESSES = [
        "cheatengine", "ollydbg", "x64dbg", "x32dbg",
        "ida", "ida64", "ida free", "idat", "idat64",
        "wireshark", "fiddler", "procmon", "processhacker",
        "httpdebugger", "charles",
    ]

    def verify(self) -> bool:
        """Run all security checks. Returns True if safe to proceed."""
        if self._debugger_present():
            return False
        if self._analysis_tools_running():
            return False
        return True

    def _debugger_present(self) -> bool:
        try:
            import ctypes
            return bool(ctypes.windll.kernel32.IsDebuggerPresent())
        except Exception:
            return False

    def _analysis_tools_running(self) -> bool:
        try:
            for proc in psutil.process_iter(["name"]):
                name = (proc.info["name"] or "").lower()
                for blocked in self.BLOCKED_PROCESSES:
                    if blocked in name:
                        return True
        except Exception:
            pass
        return False
