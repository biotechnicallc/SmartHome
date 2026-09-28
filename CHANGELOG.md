# Changelog

## 1.1

Added Home Assistant climate/thermostat support.

- Added generic Home Assistant `climate.*` entity support
- Added Home Assistant Thermostat device type
- Added thermostat status display
- Added temperature increase and decrease controls
- Added direct Set Temperature control
- Added Cool, Heat, Auto/Heat-Cool, and Off controls
- Added Fan Auto and Fan On controls
- Added Home Assistant climate entity entry when creating a thermostat
- Removed installation-specific thermostat identifiers from the application
- Added generic companion relay API support for Home Assistant climate entities
- Improved public-release protection for credentials and runtime files
- Retained existing Shelly device support and stable IDs

## 1.0

Initial public release.

- Shelly smart device support
- Add, rename, and remove devices
- On, Off, and Toggle controls
- Persistent device storage
- Configurable relay URL and API token
- Stable remote device IDs
- Editable IPv4 device entry
- FlipperHTTP Wi-Fi Developer Board integration
