# Smart Home for Flipper Zero

Smart Home is a Flipper Zero application for controlling supported smart home devices through a Wi-Fi Developer Board and an authenticated companion relay server.

## Features

- Add smart home devices from the Flipper Zero
- Store devices persistently on the SD card
- Rename and remove saved devices
- Persistent relay URL and API token configuration
- Stable remote device IDs
- Editable IPv4 device entry for supported LAN devices
- Shelly smart device support
- Home Assistant climate/thermostat support
- Generic Home Assistant `climate.*` entities
- Thermostat status, temperature, HVAC mode, and fan controls

## Requirements

- Flipper Zero
- Wi-Fi Developer Board compatible with FlipperHTTP
- FlipperHTTP firmware/library
- A reachable Smart Home relay server
- Supported smart home devices accessible by the relay
- Home Assistant only if using Home Assistant thermostat support

The companion relay is required. It handles authenticated communication between the Flipper application and supported smart home services/devices.

## First-Time Setup

1. Install `smart_home.fap` on the Flipper Zero.
2. Connect the Wi-Fi Developer Board.
3. Open **Smart Home**.
4. Open **Settings**.
5. Configure the Relay URL and API Token.
6. Return to the main menu.
7. Select **+ Add Device**.
8. Choose the desired device type.

### Adding a Shelly device

1. Choose **Shelly**.
2. Enter a device name.
3. Enter the Shelly device's IPv4 address.
4. The relay discovers the device and stores its stable Shelly device ID.

The IPv4 editor starts at `192.168.7.0` for convenience. Every octet is editable.

### Adding a Home Assistant thermostat

1. Choose **Home Assistant Thermostat**.
2. Enter a device name.
3. Enter the Home Assistant climate entity ID.

Examples:

    climate.living_room
    climate.bedroom
    climate.downstairs

The public application does not contain a hard-coded thermostat entity. Each user supplies their own Home Assistant `climate.*` entity when adding the device.

## Current Device Support

### Shelly

The relay discovers the Shelly device at the supplied IPv4 address and returns its stable Shelly device ID.

Supported actions include:

- On
- Off
- Toggle
- Rename
- Delete

### Home Assistant Climate / Thermostat

Home Assistant thermostats are addressed through generic `climate.*` entity IDs.

Supported actions include:

- Status
- Temperature +1
- Temperature -1
- Set Temperature
- Cool
- Heat
- Auto / Heat-Cool
- Off
- Fan Auto
- Fan On
- Rename
- Delete

Available HVAC and fan modes ultimately depend on the capabilities exposed by the selected Home Assistant climate entity.

## Companion Relay

The companion relay is available at:

https://github.com/biotechnicallc/SmartHome-Relay

The application communicates with the companion Smart Home relay over HTTP using bearer-token authentication.

The relay provides:

- Shelly registration and stable device IDs
- RFC1918-only Shelly registration targets
- Home Assistant API integration
- Generic `climate.*` thermostat endpoints
- Optional Home Assistant support
- Separate relay and Home Assistant credentials

Do not expose the relay directly to the public Internet. Use it on a trusted LAN, VPN, or equivalent private network.

Keep relay credentials and runtime state private.

## Configuration Storage

Smart Home stores application data under:

    /ext/apps_data/smart_home/

Current files include:

- `devices.txt`
- `settings.txt`

The relay API token is stored locally in `settings.txt`. Treat the Flipper Zero SD card as sensitive if the configured relay token grants remote-control access.

## Building

Install uFBT, then from this directory run:

    ufbt

The resulting application is created at:

    dist/smart_home.fap

To install and launch directly on a connected Flipper Zero:

    ufbt launch

## Project Structure

    application.fam
    smart_home.c
    smart_home_10px.png
    flipper_http/
        flipper_http.c
        flipper_http.h

## Third-Party Components

This project includes the MIT-licensed FlipperHTTP library by JBlanked.

See `THIRD_PARTY_NOTICES.md` for attribution and upstream project information.

## Version

Current release: `1.1`

## Author

Vincenzo
