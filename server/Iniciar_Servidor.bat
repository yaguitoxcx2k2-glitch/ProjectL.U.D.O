@echo off
cd /d "%~dp0"
where py >nul 2>nul
if errorlevel 1 (
  echo Instale Python 3.10 ou superior para Windows em python.org, incluindo Tcl/Tk e o Python Launcher.
  pause
  exit /b 1
)
py -3 ludo_server_gui.py
if errorlevel 1 pause
