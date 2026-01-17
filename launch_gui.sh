#!/bin/bash

# Stratatools GUI Launcher Script
# This script launches the Stratatools GUI application

echo "Starting Stratatools GUI..."

# Check if running from correct directory
if [ ! -f "stratatools_gui.py" ]; then
    echo "Error: stratatools_gui.py not found in current directory"
    echo "Please run this script from the stratatools root directory"
    exit 1
fi

# Check if Python 3 is available
if ! command -v python3 &> /dev/null; then
    echo "Error: python3 not found. Please install Python 3.6 or higher"
    exit 1
fi

# Check if PyQt5 is installed
if ! python3 -c "import PyQt5" 2>/dev/null; then
    echo "Error: PyQt5 not installed"
    echo "Install with: pip3 install PyQt5"
    exit 1
fi

# Launch the GUI
python3 stratatools_gui.py

echo "Stratatools GUI closed"
