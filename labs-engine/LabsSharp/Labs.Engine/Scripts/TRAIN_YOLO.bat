@echo off
REM ============================================================
REM TRAIN_YOLO.bat — Trains the Got Next pad detector
REM Double-click to run, or run from cmd
REM ============================================================

echo.
echo  ====================================================
echo   TM Labs YOLO Trainer
echo  ====================================================
echo.
echo  Training pad detector from labeled frames in:
echo    boost_court_templates\
echo.
echo  This will take 30-60 minutes on CPU.
echo  Don't close this window until you see "DEPLOYED:".
echo.
echo  ====================================================
echo.

cd /d "%~dp0"

REM Multi-class training: pad, nameplate, own_player, prompt, wall (+ any custom classes)
python train_pad_yolo.py "%~dp0boost_court_templates" --epochs 30 --imgsz 640 --batch 8

echo.
echo  ====================================================
echo   Done. Check above for "Trained model saved to:"
echo  ====================================================
echo.
pause
