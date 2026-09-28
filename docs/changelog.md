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
- Added climate entity entry when creating a thermostat
- Removed installation-specific thermostat identifiers
- Improved protection for credentials and runtime files
- Retained existing Shelly support and stable device IDs

## 1.0

Initial public release.

- Added Shelly smart device support
- Added device registration using stable remote IDs
- Added On, Off, and Toggle controls
- Added persistent device storage
- Added device rename and removal
- Added configurable relay URL and API token
- Added persistent settings storage
- Added editable IPv4 device entry
- Added first-run configuration checks
