# TwilightBoxart

A GUI tool that automatically downloads box art for your [TwilightMenu++](https://github.com/DS-Homebrew/TWiLightMenu) SD card. Point it at your SD card, hit start, and it fills in all the cover art for your ROM collection.

## Download

Grab the latest release for your platform from the [Releases](../../releases) page:

- **macOS** — `twilight-boxart-macos-arm64`
- **Linux** — `twilight-boxart-linux-x86_64`
- **Windows** — `twilight-boxart-windows-x86_64.exe`

No installation needed — just download and run.

> On macOS you may need to right-click and select "Open" the first time, since the app is not signed.

## Usage

1. Insert your DS/DSi SD card
2. Launch TwilightBoxart
3. Your SD card should be auto-detected — if not, click **Browse** or **Detect SD**, or drag & drop the folder
4. Pick a size preset and tweak settings if you want
5. *(Optional)* Paste a free [SteamGridDB API key](https://www.steamgriddb.com/) for extra box art coverage
6. Click **Start Download** (or press **Enter**)
7. Wait for it to finish, then safely eject your SD card

Your settings (SD path, API key, preferences) are saved automatically between sessions.

## Supported Systems

| System | Extensions |
|--------|-----------|
| Nintendo DS | `.nds`, `.dsi` |
| Game Boy Advance | `.gba` |
| Game Boy / Color | `.gb`, `.gbc` |
| Super Nintendo | `.sfc`, `.smc` |
| Nintendo Entertainment System | `.nes` |
| Famicom Disk System | `.fds` |
| Sega Genesis / Mega Drive | `.gen` |
| Sega Master System | `.sms` |
| Sega Game Gear | `.gg` |

## How It Works

TwilightBoxart scans your SD card for ROM files and downloads matching box art from multiple sources:

1. **GameTDB** — high-quality covers for NDS games (matched by game code from the ROM header)
2. **LibRetro Thumbnails** — community-maintained box art for all retro systems
3. **SteamGridDB** — optional fallback with broad game coverage (requires free API key)

Images are resized to fit the DS screen and saved as PNGs in the `_nds/TWiLightMenu/boxart` folder. For non-NDS ROMs, 32x32 custom icons are also generated.

## Keyboard Shortcuts

| Key | Action |
|-----|--------|
| `Enter` | Start download |
| `Escape` | Stop download |
| `Tab` | Cycle between text fields |
| `Cmd/Ctrl + V` | Paste into text field |

## Building from Source

<details>
<summary>Click to expand build instructions</summary>

### Prerequisites

- C11 compiler (GCC, Clang, or MinGW)
- [raylib](https://www.raylib.com/) 5.0+
- [libcurl](https://curl.se/libcurl/)

### CMake (recommended, all platforms)

```bash
cmake -B build
cmake --build build
```

CMake will automatically download and build raylib if not found on your system. Only libcurl needs to be installed.

### macOS

```bash
brew install raylib curl
make
```

### Linux

```bash
# Install dependencies (Ubuntu/Debian)
sudo apt-get install -y libcurl4-openssl-dev libx11-dev libxrandr-dev \
    libxinerama-dev libxcursor-dev libxi-dev libgl-dev

# Option A: Install raylib from source
git clone https://github.com/raysan5/raylib.git --depth 1 -b 5.5
cd raylib && cmake -B build && cmake --build build && sudo cmake --install build

# Option B: Just use CMake (it will fetch raylib automatically)
cmake -B build && cmake --build build
```

### Windows (MSYS2/MinGW)

```bash
pacman -S mingw-w64-x86_64-{gcc,cmake,raylib,curl}
cmake -B build -G "MinGW Makefiles"
cmake --build build
```

</details>

## License

MIT License - see [LICENSE](LICENSE) for details.
