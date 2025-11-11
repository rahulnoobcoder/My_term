# My_term

A minimal custom terminal application written in C++.

My_term provides source code and a Makefile to compile and run a small terminal-like application. This README explains how to build, run, and optionally install a desktop entry for the app.



---

## Features
- Lightweight terminal application written in C++
- Simple build with a provided Makefile
- Optional desktop integration (creates an application entry)

---

## Prerequisites / Dependencies (Ubuntu-based)
Install required packages:

sudo apt update
sudo apt install build-essential pkg-config libx11-dev \    
    libxft-dev libxrender-dev libxext-dev \    
    libcairo2-dev libpango1.0-dev

---

## Build & Run
From the project root:

make run

This will compile the project using the provided Makefile and run the application.

---

## Execution (manual)
If you prefer to build then run manually:

make
./path/to/binary   # adjust to actual binary name/location produced by the Makefile

---

## Optional: Create a Desktop Application Entry
1. Edit bat_term.desktop:
   - Change line 8 to the full path of run_app.sh on your system.
   - (Optional) Edit line 11 to set a custom logo.

2. Make scripts executable and install the desktop entry:

chmod +x run_app.sh
chmod +x bat_term.desktop
cp bat_term.desktop ~/.local/share/applications/

After this, the application should appear in your desktop environment’s applications menu (e.g., Ubuntu apps).

---

## Platform & Tooling
- Tested on: Linux (Ubuntu-based distributions)
- Compiler: g++ (provided by build-essential)
- Build tool: make
- Suggested C++ standard: C++17 (adjust if your code requires a different standard)

If your Makefile requires a specific C++ standard or flags, update the Makefile accordingly.

---

## How to Test
1. Ensure dependencies are installed.
2. Run:

make run

3. The terminal application should compile and launch.
4. If you installed the desktop entry, look for the application named bat_term (or the name in bat_term.desktop) in your system menu.

---

## Cleanup
If the Makefile produces object files or a build directory, use:

make clean

(If make clean is not defined, delete build artifacts manually.)

---

## Contributing
Contributions are welcome. If you'd like help:
- Open an issue describing the feature or bug.
- Submit a PR with a clear description of changes and how to build/test them.

---
