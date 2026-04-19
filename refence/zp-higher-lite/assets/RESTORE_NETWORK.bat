@echo off
echo ===========================================
echo   ZP Vision - Network Settings Restore
echo ===========================================
echo.
echo Restoring default Windows network settings...
echo.

:: Restore TCP Auto-Tuning
netsh int tcp set global autotuninglevel=normal
echo [OK] TCP Auto-Tuning restored to Normal

:: Remove Nagle override
reg delete "HKLM\SYSTEM\CurrentControlSet\Services\Tcpip\Parameters" /v TcpNoDelay /f 2>nul
echo [OK] Nagle's Algorithm restored

:: Remove ACK frequency override  
reg delete "HKLM\SYSTEM\CurrentControlSet\Services\Tcpip\Parameters" /v TcpAckFrequency /f 2>nul
echo [OK] TCP ACK delay restored

:: Restore network throttling
reg delete "HKLM\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Multimedia\SystemProfile" /v NetworkThrottlingIndex /f 2>nul
echo [OK] Network throttling restored

:: Restore TCP timestamps
reg delete "HKLM\SYSTEM\CurrentControlSet\Services\Tcpip\Parameters" /v Tcp1323Opts /f 2>nul
echo [OK] TCP timestamps restored

echo.
echo ===========================================
echo   All network settings restored!
echo   Restart your PC for full effect.
echo ===========================================
pause
