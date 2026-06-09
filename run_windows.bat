@echo off
REM ============================================================
REM  Stratatools GUI - Windows setup + launcher
REM  First run creates a local virtual environment and installs
REM  the dependencies; later runs just launch the GUI.
REM ============================================================
cd /d "%~dp0"

where python >nul 2>&1
if errorlevel 1 (
    echo.
    echo Error: Python not found.
    echo Install Python 3.8 or newer from https://www.python.org/downloads/
    echo and make sure "Add python.exe to PATH" is checked during install.
    echo.
    pause
    exit /b 1
)

if not exist ".venv\Scripts\python.exe" (
    echo Creating virtual environment ^(first run only^)...
    python -m venv .venv
    if errorlevel 1 (
        echo Error: could not create the virtual environment.
        pause
        exit /b 1
    )
    echo Installing dependencies ^(first run only, may take a minute^)...
    ".venv\Scripts\python.exe" -m pip install --upgrade pip
    ".venv\Scripts\python.exe" -m pip install -r requirements.txt
    if errorlevel 1 (
        echo Error: dependency installation failed.
        pause
        exit /b 1
    )
)

echo Launching Stratatools GUI...
".venv\Scripts\python.exe" stratatools_gui.py
if errorlevel 1 (
    echo.
    echo The GUI exited with an error. See the messages above.
    pause
)
