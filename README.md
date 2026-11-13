# SimHub Yellow TFT Dashboard

A small ESP32 project for the ESP32-2432S028 Cheap Yellow Display (CYD) that shows:

- Engine RPM
- Gear
- Speed
- Lap time

This project receives telemetry over UDP from SimHub and renders it on the CYD's 320x240 landscape ILI9341 display, flipped 180 degrees. A `Connecting...` screen remains visible while WiFi connects.

## Hardware

- ESP32-2432S028 CYD (ILI9341)

The display uses the CYD's onboard wiring, configured in `src/User_Setup.h`.

## Project structure

- `platformio.ini` – PlatformIO project config
- `src/main.cpp` – dashboard firmware
- `simhub_test_sender.py` – quick UDP sender for local testing without SimHub

## Setup

1. Install PlatformIO for VS Code.
2. Open this folder in VS Code.
3. Update WiFi credentials in `src/main.cpp`.
4. Set `PC_IP` in `src/main.cpp` to your PC's LAN IP address.
5. The TFT_eSPI CYD setup is included for every source file by `platformio.ini`.
6. Run:

```bash
pio run -t upload
```

## SimHub data format

The ESP32 expects a JSON UDP packet like this:

```json
{"rpm":6800,"gear":3,"speed":142.8,"lapTime":83.254}
```

Send that packet to the ESP32 IP address and UDP port `9000`.

## Local test mode

You can test the dashboard without SimHub by running:

```bash
python3 simhub_test_sender.py
```

Then point it at the ESP32 IP. If you are testing on the same machine, set the IP to `127.0.0.1` and confirm the UDP port is correct.

## Example SimHub output

In SimHub, send the values in JSON to your ESP32 over UDP. A simple payload payload is enough:

```json
{"rpm": 6850, "gear": 3, "speed": 118.4, "lapTime": 91.432}
```

This gives a clean dashboard display with live telemetry.

## Touch controls

Tap anywhere on the dashboard to open the six-button menu: Pit Limiter,
Headlights, Wipers, Ignition, Starter, and Reset Car. On first boot, touch
calibration appears; tap and hold each of its four targets. Calibration is
saved in the CYD's flash for later boots. The 180-degree screen flip requires
one fresh touch calibration after updating.

The buttons send JSON commands over UDP to the PC on port `9001`. The SimHub
plugin below receives the commands and turns them into key presses. Set
`PC_IP` in `src/main.cpp` to the PC's LAN IP.

## SimHub plugin

`simhub-plugin/SimHub.CYDDashboard` contains the C# plugin. It sends RPM, gear,
speed, and current lap time to the CYD at 10 Hz, and receives the touch menu
commands on UDP port `9001`.

Build it on the Windows PC with Visual Studio 2022 and the .NET Framework 4.8
developer pack. The project expects SimHub in `C:\Program Files (x86)\SimHub`;
if it is installed elsewhere, set `SimHubInstallPath` in the project build.
Copy `SimHub.CYDDashboard.dll` beside `SimHubWPF.exe` and restart SimHub.
SimHub's SDK demo projects are installed in its `PluginSdk` folder.

On first start, the plugin creates this settings file:

```text
%LOCALAPPDATA%\SimHub\PluginsData\CYDDashboard\settings.ini
```

Set `EspIp` to the ESP32's IP address from its serial monitor. Edit the six
`*Key` values to match your game's keyboard bindings, then restart SimHub.
The game must accept keyboard input for those bindings.

The Python command receiver remains available as an alternative to the plugin:

```bash
python -m pip install pynput
python simhub_command_receiver.py
```

Edit `COMMAND_KEYS` in `simhub_command_receiver.py` to match your game's key
bindings. Keep the game window focused; the receiver turns each UDP command
into a single key press. Allow UDP port `9001` through the PC firewall if
needed.
