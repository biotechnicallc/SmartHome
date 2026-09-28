# Smart Home

Smart Home lets you control supported smart home devices from your Flipper Zero using a Wi-Fi Developer Board and an authenticated companion relay server.

## Features

- Add and save supported smart home devices
- Rename and remove saved devices
- Configure a remote relay URL and API token
- Store stable remote device IDs
- Persistent device and settings storage
- Editable IPv4 address entry
- Shelly smart device support
- Home Assistant climate/thermostat support
- Generic Home Assistant `climate.*` entity support
- Thermostat status and temperature controls
- HVAC mode controls
- Fan mode controls

## Shelly Support

Shelly devices can be added by IPv4 address.

The relay discovers the device and stores its stable Shelly device ID.

Supported actions include:

- On
- Off
- Toggle

## Home Assistant Thermostat Support

Home Assistant thermostats can be added using their `climate.*` entity ID.

Examples:

    climate.living_room
    climate.bedroom
    climate.downstairs

Supported thermostat actions include:

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

Available HVAC and fan modes depend on the capabilities of the selected Home Assistant climate entity.

A compatible FlipperHTTP Wi-Fi Developer Board and reachable Smart Home relay server are required.

Home Assistant is only required when using Home Assistant thermostat support.
