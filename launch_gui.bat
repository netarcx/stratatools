@echo off
REM Stratatools GUI Launcher Script for Windows
REM This script launches the Stratatools GUI application

echo Starting Stratatools GUI...

REM Check if Python 3 is available
python --version >nul 2>&1
if errorlevel 1 (
    echo Error: Python not found. Please install Python 3.6 or higher
    pause
    exit /b 1
)

REM Check if stratatools_gui.py exists
if not exist "stratatools_gui.py" (
    echo Error: stratatools_gui.py not found in current directory
    echo Please run this script from the stratatools root directory
    pause
    exit /b 1
)

REM Launch the GUI
python stratatools_gui.py

echo Stratatools GUI closed
pause
