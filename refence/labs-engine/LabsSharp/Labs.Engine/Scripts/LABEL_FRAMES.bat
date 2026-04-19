@echo off
REM ============================================================
REM LABEL_FRAMES.bat — Opens the multi-class labeler
REM Double-click this file from Explorer to launch
REM ============================================================

echo.
echo  ====================================================
echo   TM Labs Multi-Class Labeler
echo  ====================================================
echo.
echo  Opening labeler window...
echo.
echo  CONTROLS:
echo    1-5         = switch class (1=pad, 2=nameplate, 3=own_player, 4=prompt, 5=wall)
echo    drag mouse  = draw box
echo    right-click = undo last box
echo    J           = AUTO: run YOLO and pre-fill boxes
echo    C           = clear all boxes on this frame
echo    SPACE       = save and next
echo    BACKSPACE   = previous
echo    S           = skip (save empty)
echo    Q           = quit and save
echo.
echo  ====================================================
echo.

cd /d "%~dp0"

python label_objects.py "%~dp0boost_court_templates"

echo.
echo  Labeler closed.
pause
