# Loom Pi Installation Guide (Raspberry Pi)

This guide walks you through installing and building **Loom Pi** from scratch on a fresh Raspberry Pi OS installation.

## 1. Prepare the Raspberry Pi
1. Flash your microSD card with **Raspberry Pi OS** (Bookworm or Bullseye, 64-bit recommended) using the Raspberry Pi Imager. 
2. Boot up the Pi, complete the initial setup (Wi-Fi, localization, password), and ensure it has internet access.

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
