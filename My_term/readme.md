Run these commmands to get the required packages 

sudo apt install build-essential pkg-config libx11-dev
sudo apt install libxft-dev libxrender-dev libxext-dev
sudo apt install libcairo2-dev libpango1.0-dev

run this below command in your directory directly after installing packages 
make run



//optional to make a Desktop app
Edit line 8 of bat_term.desktop with path to the run_app.sh on your system (Even line 11 if you want a logo to your application).
If you want to clean history of the terminal on every run you can uncomment/use corresponding section in the clean part of MakeFile

Run these commands in your directory 

chmod +x run_app.sh
chmod +x bat_term.desktop
cp bat_term.desktop ~/.local/share/applications/

we can now see the desktop application in the ubuntu apps.