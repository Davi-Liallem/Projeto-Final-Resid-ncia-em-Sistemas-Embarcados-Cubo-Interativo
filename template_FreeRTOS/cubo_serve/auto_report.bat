@echo off
cd /d C:\cubo_serve
:loop
python report.py >nul 2>&1
timeout /t 2 /nobreak >nul
goto loop
