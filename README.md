My_term - A C++ Terminal Application
My_term is a custom terminal application built in C++. This project provides the necessary code and build instructions to compile and run the terminal, as well as optional steps to integrate it as a desktop application on Linux.

COMPILATION INSTRUCTIONS

Make sure all dependencies listed in section 3 are installed.

Compile and run using the provided Makefile:

make run
EXECUTION INSTRUCTIONS

Run the program directly from your directory using:

make run
Optional: Create a Desktop Application

Edit line 8 of bat_term.desktop with the full path to the run_app.sh script on your system.

(Optional) Edit line 11 if you want to set a custom logo for the application.

Run the following commands in your directory to install the desktop entry:

chmod +x run_app.sh
chmod +x bat_term.desktop
cp bat_term.desktop ~/.local/share/applications/
The application should now be visible in your system's application menu (e.g., Ubuntu apps).

DEPENDENCIES

Run the following commands to install the required packages on an Ubuntu-based system:

sudo apt update
sudo apt install build-essential pkg-config libx11-dev
sudo apt install libxft-dev libxrender-dev libxext-dev
sudo apt install libcairo2-dev libpango1.0-dev
PLATFORM INFORMATION

Tested On:

Linux (Ubuntu-based distributions)

Compiler/Build:

g++ (via build-essential)

make

C++ Standard:

(Please add your C++ standard, e.g., C++17)

HOW TO TEST

Install all dependencies listed in section 3.

Run make run in the project directory.

The terminal application should compile and launch.

If you followed the optional desktop application steps, find and launch "bat_term" from your system's application menu.

CLEANUP (IF REQUIRED)
