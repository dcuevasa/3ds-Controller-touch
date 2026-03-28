# 3DS Controller

Use your Nintendo 3DS as a wireless controller for PC games and emulators through DSU/Cemuhook.

This README is divided into a **Simple Non-Technical Setup** and **Technical Details**.

---

## 🎮 Easy Non-Technical Setup

You don't need any programming skills to use this! You can download the application already compiled and ready to play.

### 1. Download

Download the files directly from the [Releases page (v.0.0.1)](https://github.com/dcuevasa/3ds-Controller-touch/releases/tag/v.0.0.1):

- **If you use Homebrew Launcher:** Download the `.3dsx` file here: [3ds_controller.3dsx](https://github.com/dcuevasa/3ds-Controller-touch/releases/download/v.0.0.1/3ds_controller.3dsx)
- **If you use FBI (Home Screen Icon):** Download the `.cia` file here: [3ds_controller.cia](https://github.com/dcuevasa/3ds-Controller-touch/releases/download/v.0.0.1/3ds_controller.cia)

### 2. Requirements

- A hacked Nintendo 3DS
- Your PC and 3DS must be connected to the **exact same Wi-Fi**.
- Cemu (or any other DSU-compatible emulator).

### 3. Quick Setup

1. Copy the downloaded file to your 3DS SD Card and launch it.
2. Open Cemu on your PC, go to Input Settings, and add a generic DSUClient.
3. Look at your 3DS screen, it will show your **IP Address**. Enter that IP in Cemu.
4. Set the Port to `26760` (it's the default).
5. Done! You can now map the buttons, touchscreen, and motion controls.

---

## Usage

1. Start the app on your 3DS.
2. Connect from your DSU client on PC.
3. Play using Circle Pad, C-Stick, buttons, touch, and gyro.
4. Hold SELECT for around 5 seconds to toggle LCD backlight.
5. Press START + SELECT to exit.

## Control Mapping

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

## Optional Configuration

You can create `/config.ini` on the SD card to customize behavior.

Supported keys:
- `targetip=<IPv4>` (enables IP filter only when explicitly set)
- `port=26760`
- `invertcpady=0`
- `invertcsticky=0`

If `config.ini` is missing, built-in defaults are used:
- IP filter: OFF (allow all clients)
- Port: `26760`
- `invertcpady=0`
- `invertcsticky=0`

## Troubleshooting

If your DSU client does not connect:
- Confirm both devices are on the same subnet.
- Check firewall rules for UDP port `26760`.
- Use `targetip=0.0.0.0` (or leave it unset) to allow all clients.

---

## Technical Details: Building from Source

If you prefer to compile the application yourself instead of using the provided pre-built releases, please follow these instructions. 

### Development Requirements

#### 3DS Application
- DevkitPro and DevkitARM
- 3DS development libraries (libctru)
- Make utility

### Complete WSL2 Ubuntu 24.04 Setup (devkitPro + 3DS)

This is the recommended full setup for Ubuntu 24.04 on WSL2.
It avoids the outdated install script path and follows modern APT keyring security requirements.

#### Step 1: Update System and Install Basic Dependencies
```bash
sudo apt update && sudo apt upgrade -y
sudo apt install -y wget curl make git gnupg
```

#### Step 2: Securely Add the devkitPro APT Repository
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

#### Step 3: Install devkitPro Pacman
```bash
sudo apt update
sudo apt install -y devkitpro-pacman
```

#### Step 4: Install the Full 3DS Toolchain
```bash
sudo dkp-pacman -S 3ds-dev
```

Important notes:
- Press Enter when prompted to select package groups (default: all).
- Press Y to confirm install.
- Do not append `makerom` or `bannertool` to that command.
  - In this project, files named `makerom` and `bannertool` exist in the repository root.
  - Appending those names can make pacman treat them as local file targets instead of packages.

#### Step 5: Configure Environment Variables
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

#### Step 6: Build This Project and Generate .cia

To compile the application simply run `make cia`.

```bash
cd ~/3ds-Controller-touch
chmod +x makerom bannertool
make clean
make cia
```

Output files:
- `3ds_controller.3dsx` (Homebrew format)
- `3ds_controller.cia` (installable CIA for FBI)

