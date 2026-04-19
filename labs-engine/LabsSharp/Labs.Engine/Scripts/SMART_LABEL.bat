@echo off
REM ============================================================
REM SMART_LABEL.bat — Click-to-label with template matching
REM Double-click this file from Explorer to launch
REM ============================================================

echo.
echo  ====================================================
echo   TM Labs Smart Labeler
echo  ====================================================
echo.
echo  CLICK on a pad to teach the system. The next frame
echo  will be pre-labeled automatically using everything
echo  you've taught it so far.
echo.
echo  CONTROLS:
echo    LEFT CLICK    = extract template + auto-find similar
echo    RIGHT CLICK   = remove the box under cursor
echo    1-5           = switch class (1=pad, 2=nameplate, etc.)
echo    A             = re-run auto-find on this frame
echo    C             = clear all boxes on this frame
echo    R             = reset templates for current class
echo    SPACE         = save and next frame
echo    BACKSPACE     = previous
echo    S             = skip frame (save empty)
echo    Q             = quit
echo.
echo  ====================================================
echo.

cd /d "%~dp0"

python smart_label.py "%~dp0boost_court_templates"

echo.
echo  Smart Labeler closed.
pause
