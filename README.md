# Smart Home for Flipper Zero

Smart Home is a Flipper Zero application for controlling supported smart home devices through a Wi-Fi Developer Board.

## Features

- Add smart home devices from the Flipper Zero
- Store devices persistently on the SD card
- Rename and remove saved devices
- Control supported devices remotely
- Persistent relay URL and API token configuration
- Stable device IDs returned by the relay
- Editable IPv4 device entry
- Shelly smart device support

## Requirements

- Flipper Zero
- Wi-Fi Developer Board compatible with FlipperHTTP
- FlipperHTTP firmware/library
- A reachable Smart Home relay server
- Supported smart home devices on the relay's local network

## First-Time Setup

1. Install `smart_home.fap` on the Flipper Zero.
2. Connect the Wi-Fi Developer Board.
3. Open **Smart Home**.
4. Open **Settings**.
5. Configure the Relay URL and API Token.
6. Return to the main menu.
7. Select **+ Add Device**.
8. Choose the supported device type.
9. Enter a name and IPv4 address.

The IPv4 editor starts at `192.168.7.0` for convenience. Every octet is editable.

## Current Device Support

### Shelly

The relay discovers the Shelly device at the supplied IPv4 address and returns its stable Shelly device ID.

Supported actions currently include:

- On
- Off
- Toggle

## Configuration Storage

Smart Home stores application data under:

    /ext/apps_data/smart_home/

Current files include:

- `devices.txt`
- `settings.txt`

The API token is stored locally in `settings.txt`. Treat the Flipper Zero SD card as sensitive if the configured relay token grants remote control access.

## Building

Install uFBT, then from this directory run:

    ufbt

The resulting application is created at:

    dist/smart_home.fap

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

Current release: `1.0.0`

## Author

Vincenzo
