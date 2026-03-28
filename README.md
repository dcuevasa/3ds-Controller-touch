# 3DS Controller
An up-to-date 3DS homebrew application that lets you use your Nintendo 3DS as a wireless controller for Windows and Linux.

## Features
- Use your 3DS as a wireless gamepad for PC games and emulators
- Cross-platform support for Windows and Linux
- Support for all buttons, Circle Pad, and C-Stick
- Configurable server IP and port
- Low latency wireless connection
- Battery level indicator
- LCD toggle option to save battery

## Requirements

### 3DS
- Nintendo 3DS with custom firmware (CFW)
- Homebrew Launcher access
- Wi-Fi connection (same network as PC)

### PC
- Cemu (or another DSU/Cemuhook-compatible client)
- Same local network as your 3DS
- Firewall rule allowing incoming UDP on port 26760 (if needed)

## Quickstart (DSU / Cemu)

This project now acts as a DSU (Cemuhook) server directly on the 3DS.

1. Build the app (`make cia` or `make`) and install/run it on your 3DS.
2. Make sure your 3DS and PC are on the same network.
3. Launch the app on your 3DS and keep it running.
4. In Cemu, open Input settings and choose the DSU/Cemuhook client API (name can vary by version).
5. Set DSU host to your 3DS IP and port to `26760`.
6. Select slot `0`, apply settings, and test sticks/buttons/motion.

If Cemu cannot connect:
- Confirm both devices are on the same subnet.
- Check that UDP port `26760` is not blocked by your firewall.
- Set `targetip=0.0.0.0` in `config.ini` to allow all DSU clients.

## Usage
1. Start the 3DS app.
2. Connect from your DSU client (for example Cemu).
3. Use Circle Pad/C-Stick/Buttons/Touch/Gyro in-game.
4. Hold SELECT for about 5 seconds to toggle LCD backlight.
5. Press START + SELECT to exit.

### Control Mapping

| 3DS Input | DSU Output |
|-----------|------------|
| A/B/X/Y Buttons | Face buttons (Cross/Circle/Square/Triangle style) |
| Circle Pad | Left analog stick |
| C-Stick | Right analog stick |
| D-Pad | D-Pad |
| L/R Buttons | L1/R1 |
| ZL/ZR Buttons | L2/R2 |
| Start/Select | Options/Share |
| Touchscreen | DS4 touch data |
| Gyroscope + Accelerometer | DSU motion data |

## Configuration

### 3DS
- Config file at `/config.ini` on SD card
- Optional: if missing, the app runs with built-in defaults
- Supported keys:
  - `targetip=<IPv4>` (enables IP filter only when explicitly set)
   - `port=26760` (DSU default)
   - `invertcpady=0`
   - `invertcsticky=0`

Built-in defaults when `config.ini` is missing:
- IP filter: OFF (allow all clients)
- Port: `26760`
- `invertcpady=0`
- `invertcsticky=0`

### PC
- Configure your DSU client to connect to the 3DS server
- Host/IP: your 3DS local IP
- Port: `26760`
- Slot: `0`

## Building from Source

### Requirements

#### 3DS Application
- DevkitPro and DevkitARM
- 3DS development libraries (libctru)
- Make utility

#### PC Side (DSU Client)
- Cemu or another DSU/Cemuhook-compatible client
- Same local network as the 3DS
- UDP access to port 26760

## Complete WSL2 Ubuntu 24.04 Setup (devkitPro + 3DS)

This is the recommended full setup for Ubuntu 24.04 on WSL2.
It avoids the outdated install script path and follows modern APT keyring security requirements.

### Step 1: Update System and Install Basic Dependencies
```bash
sudo apt update && sudo apt upgrade -y
sudo apt install -y wget curl make git gnupg
```

### Step 2: Securely Add the devkitPro APT Repository
The old devkitPro installer flow can fail on Ubuntu 24.04 due to stricter key handling.
Use a signed keyring file instead.

1. Download and convert the key:
```bash
sudo mkdir -p /usr/share/keyrings
wget -qO- https://apt.devkitpro.org/devkitpro-pub.gpg | sudo gpg --dearmor --yes -o /usr/share/keyrings/devkitpro-archive-keyring.gpg
```

If the direct download fails, download `devkitpro-pub.gpg` manually and run:
```bash
sudo gpg --dearmor --yes -o /usr/share/keyrings/devkitpro-archive-keyring.gpg devkitpro-pub.gpg
```

2. Add the signed repository entry:
```bash
echo "deb [signed-by=/usr/share/keyrings/devkitpro-archive-keyring.gpg] https://apt.devkitpro.org stable main" | sudo tee /etc/apt/sources.list.d/devkitpro.list
```

### Step 3: Install devkitPro Pacman
```bash
sudo apt update
sudo apt install -y devkitpro-pacman
```

### Step 4: Install the Full 3DS Toolchain
```bash
sudo dkp-pacman -S 3ds-dev
```

Important notes:
- Press Enter when prompted to select package groups (default: all).
- Press Y to confirm install.
- Do not append `makerom` or `bannertool` to that command.
  - In this project, files named `makerom` and `bannertool` exist in the repository root.
  - Appending those names can make pacman treat them as local file targets instead of packages.

### Step 5: Configure Environment Variables
```bash
echo 'export DEVKITPRO=/opt/devkitpro' >> ~/.bashrc
echo 'export DEVKITARM=${DEVKITPRO}/devkitARM' >> ~/.bashrc
echo 'export PATH=${DEVKITARM}/bin:${DEVKITPRO}/tools/bin:$PATH' >> ~/.bashrc
source ~/.bashrc
```

You can verify the toolchain with:
```bash
arm-none-eabi-gcc --version
```

### Step 6: Build This Project and Generate .cia
```bash
cd ~/3ds-Controller-touch
chmod +x makerom bannertool
make clean
make cia
```

Output files:
- `3ds_controller.3dsx` (Homebrew format)
- `3ds_controller.cia` (installable CIA for FBI)

