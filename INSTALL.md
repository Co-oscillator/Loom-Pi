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

## 2. Update the System
Open a terminal and update your package lists to ensure everything is current:
```bash
sudo apt update
sudo apt upgrade -y
```

## 3. Install Dependencies
Loom Pi relies on CMake, SDL2 (for windowing/graphics), and ALSA (for audio and MIDI on Linux). Install all required build tools and libraries with the following command:
```bash
sudo apt install -y build-essential cmake git libsdl2-dev libasound2-dev
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
