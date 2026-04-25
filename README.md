# ELM327 CAN to BLE Bridge

This project uses an ELM327 Wi-Fi adapter to read CAN bus data, process incoming messages, and broadcast them via BLE (Bluetooth Low Energy) to other devices.

## Features
- Connects to a vehicle's CAN bus through an ELM327 Wi-Fi adapter.
- Reads and parses CAN bus messages.
- Processes and filters data as needed.
- Transmits processed data over BLE to compatible devices (e.g., smartphones, tablets, or custom BLE clients).

## Use Cases
- Real-time vehicle diagnostics over BLE.
- Forwarding CAN bus data to custom BLE applications.
- Building mobile apps that receive live car data wirelessly.

## Use Cases
- Real-time vehicle diagnostics over BLE.
- Forwarding CAN bus data to custom BLE applications.
- Building mobile apps that receive live car data wirelessly.
- Retrieving hybrid battery charge level (SOC) from a Toyota Corolla Hybrid.
- Displaying SOC on a screen as a circular progress indicator with percentage using the LVGL graphics library.

## Requirements
- ELM327 Wi-Fi adapter.
- ESP32/ESP32-C3 or C6 (or compatible board with BLE support).
- BLE-enabled client device (smartphone or custom hardware).

## Future Improvements
- Support for additional OBD-II protocols.
- Customizable CAN filters.
- Bi-directional communication (BLE to CAN).



## License
MIT License.
