# Loom Pi Installation Guide (Raspberry Pi)

This guide walks you through installing and building **Loom Pi** from scratch on a fresh Raspberry Pi OS installation.

## 1. Prepare the Raspberry Pi
1. Open the **Raspberry Pi Imager** on your main computer.
2. Select your OS (Raspberry Pi OS Lite 64-bit is recommended for a dedicated appliance).
3. **Headless Setup (No Keyboard Required):** Before hitting "Write", click the **Gear Icon** (OS Customization settings) in the Imager. 
    * Set the hostname (e.g., `loompi`).
    * Check **Enable SSH** and use password authentication.
    * Enter your Wi-Fi network name and password.
4. Flash the microSD card, put it in your Pi, and power it on. It will automatically connect to your Wi-Fi!
5. On your main computer, open a terminal and SSH into the Pi using the hostname you created:
   `ssh pi@loompi.local` (or whatever username/hostname you set).
   *Note: If the hostname doesn't resolve, you can find the Pi's IP address by logging into your router's admin page or using a network scanner app like Fing.*
   
**Important Hardware Note:** While a keyboard is never required to use or install Loom Pi, the device **must have either a touchscreen display or a USB mouse attached** out of the box in order for you to interact with the Loom interface!

## 2. Update the System
Open a terminal and update your package lists to ensure everything is current:
```bash
sudo apt update
sudo apt upgrade -y
```

## 3. Install Dependencies
Loom Pi relies on CMake, SDL2 (for windowing/graphics), and ALSA (for audio and MIDI on Linux). Install all required build tools, libraries, and Bluetooth drivers with the following command:
```bash
sudo apt install -y build-essential cmake git libsdl2-dev libasound2-dev pi-bluetooth bluez
```

## 4. Clone the Repository
Clone the Loom Pi source code from GitHub:
```bash
git clone https://github.com/Co-oscillator/Loom-Pi.git
cd Loom-Pi
```

## 5. Build the Project
We use CMake to configure the build and make to compile it. Since the Raspberry Pi 4/5 has 4 CPU cores, we can speed up compilation using `-j4`.

```bash
mkdir build
cd build
cmake ..
make -j4
```
*(Note: The first time you run `cmake ..`, it will automatically download the LVGL UI library dependencies. This might take a minute.)*

## 6. Run Loom Pi
Once the compilation finishes successfully, the `LoomPi` executable will be placed in your `build` directory. 

You can run it using:
```bash
./LoomPi
```

## Troubleshooting
* **Audio/MIDI not working:** Ensure your USB audio interface or MIDI controller is plugged in before starting the application. ALSA will automatically detect connected MIDI hardware.
* **Display Issues:** Loom Pi uses SDL2 for its graphical backend. If you are running headless (without a desktop environment), you may need to configure SDL2 to use the KMS/DRM framebuffer drivers, but running it from the standard Raspberry Pi OS Desktop environment is fully supported out of the box.
* **Squashed UI / Unused Screen Space:** Most 7-inch portable displays have a native resolution of 1024x600. However, this is not a standard HDMI resolution, so the Raspberry Pi often defaults to 1024x768 or 1280x720, which squashes the UI and leaves unused black bars. To fix this, you need to force the Pi to output exactly 1024x600.
  - **For newer Pi OS (Bookworm) using KMS/DRM:** Run `sudo nano /boot/firmware/cmdline.txt`. At the very end of the single line of text (do NOT press Enter to make a new line), add a space and then add: `video=HDMI-A-1:1024x600M@60D`
  - **For older Pi OS (Bullseye) using Legacy graphics:** Run `sudo nano /boot/config.txt` and add to the bottom: `hdmi_group=2`, `hdmi_mode=87`, `hdmi_cvt 1024 600 60 6 0 0 0`
  *(Then reboot your Pi).*

## 7. Auto-Start on Boot (Optional)
To run Loom Pi automatically when the Raspberry Pi turns on (and run it in the background without tying up your SSH session), you can create a systemd service.

1. Open a new service file:
   ```bash
   sudo nano /etc/systemd/system/loompi.service
   ```
2. Paste the following configuration. **IMPORTANT: Replace `pi` with your actual username (e.g. `loom`) in the three places below!**
   ```ini
   [Unit]
   Description=Loom Pi
   After=sound.target network.target bluetooth.target

   [Service]
   Type=simple
   User=pi
   WorkingDirectory=/home/pi/Loom-Pi/build
   ExecStart=/home/pi/Loom-Pi/build/LoomPi
   Restart=always
   RestartSec=3

   [Install]
   WantedBy=multi-user.target
   ```
3. Save (`Ctrl+O`, `Enter`) and Exit (`Ctrl+X`).
4. Enable the service so it starts on boot, and start it right now:
   ```bash
   sudo systemctl daemon-reload
   sudo systemctl enable loompi.service
   sudo systemctl start loompi.service
   ```
   
*Note: Now that it runs as a service, the console logs will automatically be saved by the system. You can view the live logs anytime over SSH by running: `journalctl -u loompi.service -f`*

## 8. Network File Sharing (Samba)
To easily transfer audio samples, projects, and configurations from your main computer to the Loom Pi using your standard file browser (macOS Finder, Windows Explorer), you can set up a Samba share.

1. Install Samba on the Raspberry Pi:
   ```bash
   sudo apt install -y samba samba-common-bin
   ```
2. Open the Samba configuration file:
   ```bash
   sudo nano /etc/samba/smb.conf
   ```
3. Scroll all the way to the bottom and paste this configuration block (replace `loom` with your actual username if different):
   ```ini
   [LoomPi]
   path = /home/loom/Loom-Pi
   writeable = yes
   create mask = 0777
   directory mask = 0777
   public = no
   ```
4. Save (`Ctrl+O`, `Enter`) and Exit (`Ctrl+X`).
5. Set a Samba password for your user (you will be prompted to type it twice):
   ```bash
   sudo smbpasswd -a loom
   ```
6. Restart the Samba service to apply the changes:
   ```bash
   sudo systemctl restart smbd
   ```

You can now connect to your Loom Pi from your main computer:
* **macOS:** Open Finder, press `Cmd + K`, and connect to `smb://loompi.local`.
* **Windows:** Open File Explorer and type `\\loompi.local` in the address bar.
*(Log in using the username `loom` and the Samba password you just created).*
