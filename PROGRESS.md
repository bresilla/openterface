# CH9329 Communication Analysis - Openterface QT

## 1. Serial Port Setup and Configuration

### Port Detection
- **VID:PID**: `0x1A86:0x7523` (CH340 USB-to-Serial chip)
- **Default Baudrate**: 115200
- **Original Baudrate**: 9600 (fallback)
- **Port Settings**: 8N1 (8 data bits, no parity, 1 stop bit - standard QSerialPort defaults)

### Serial Port Initialization
1. Port is detected by matching VID:PID combination
2. Opens port at 115200 baud first
3. If no response, falls back to 9600 baud
4. Sets RTS (Request To Send) to false after opening

## 2. CH9329 Initialization Sequence

### Complete Initialization Flow:
1. **Open Serial Port** at 115200 baud
2. **Send CMD_GET_PARA_CFG** (`57 AB 00 08 00`) to check current configuration
3. **Verify Mode**: Check if mode is 0x82 (or configured mode from settings)
4. If mode is incorrect:
   - **Reconfigure chip** with CMD_SET_PARA_CFG
   - **Reset chip** with CMD_RESET
   - **Reopen port** at 115200 baud
5. **Connect signals** for data ready and bytes written
6. **Send CMD_GET_INFO** (`57 AB 00 01 00`) to verify connection

### Mode Configuration
- Default operating mode: `0x82`
- Mode is stored at byte index 5 in the configuration command

## 3. Mouse Command Protocol

### Mouse Movement Prefixes
- **Absolute Mouse**: `57 AB 00 04 07 02`
- **Relative Mouse**: `57 AB 00 05 05 01`

### Absolute Mouse Command Structure
```
[Prefix: 6 bytes] [Button: 1 byte] [X-Low: 1 byte] [X-High: 1 byte] [Y-Low: 1 byte] [Y-High: 1 byte] [Wheel: 1 byte] [Checksum: 1 byte]
```

Example for absolute mouse at (100, 200) with left click:
```
57 AB 00 04 07 02 01 64 00 C8 00 00 [checksum]
```

### Relative Mouse Command Structure
```
[Prefix: 6 bytes] [Button: 1 byte] [dX: 1 byte] [dY: 1 byte] [Wheel: 1 byte] [Checksum: 1 byte]
```

### Mouse Button Mapping
- Left Button: `0x01` (Qt::LeftButton)
- Right Button: `0x02` (Qt::RightButton)
- Middle Button: `0x04` (Qt::MiddleButton)
- No Button: `0x00`

### Wheel Movement
- Positive values: Forward scroll (delta/100)
- Negative values: Backward scroll (0xFF - abs(delta)/100 + 1)

## 4. Response Handling and Acknowledgment

### Command Response Format
- All responses start with `57 AB`
- Response code is original command code OR'd with 0x80
- Status byte at position 5 indicates success/failure

### Status Codes
- `0x00`: Success
- `0xE1`: Serial receive timeout
- `0xE2`: Header bytes error
- `0xE3`: Command code error
- `0xE4`: Checksum error
- `0xE5`: Parameter error
- `0xE6`: Execution error

### Target Connection Detection
- CMD_GET_INFO response includes target connection status
- Byte 6 (targetConnected): 0x01 = connected, 0x00 = disconnected
- Regular polling every 3 seconds to maintain connection status

## 5. Timing Requirements

### Command Delays
- Default command delay: 0ms (configurable via `setCommandDelay()`)
- Click interval for mouse events: Configurable (default from settings)
- Key interval for keyboard events: Configurable (default from settings)
- Serial port restart delay: 1 second
- USB restart delay: 500ms

### Connection Monitoring
- Serial port check interval: 5000ms
- Target connection check: Every 3 seconds
- Connection timeout: 5 seconds (considers port disconnected)

## 6. Communication Interface

### CH9329 Communication Method
- **Interface**: Serial UART (not HID)
- **Protocol**: Custom binary protocol with checksums
- **Mode**: Configurable (default 0x82)

### Why Serial Instead of HID
- CH9329 appears as a USB-to-Serial converter to the host
- All HID commands are sent via serial and CH9329 translates them to USB HID
- This allows programmatic control without OS HID driver conflicts

## 7. Chip Mode Configuration

### Configuration Command (CMD_SET_PARA_CFG)
```
57 AB 00 09 32 [mode] [cfg] 80 00 00 01 C2 00 08 00 00 03 86 1A 29 E1 [keyboard settings] [filters] [speed mode] [reserved]
```

Key configuration bytes:
- Byte 5: Mode (0x82 default)
- Bytes 8-11: Baudrate (little-endian)
- Bytes 16-17: VID (0x1A86)
- Bytes 18-19: PID (0xE129)

## 8. Complete Mouse Movement Example

### Sending Absolute Mouse Movement to (500, 300) with Left Click:

1. **Construct command**:
   ```
   Prefix: 57 AB 00 04 07 02
   Button: 01 (left click)
   X: F4 01 (500 in little-endian)
   Y: 2C 01 (300 in little-endian)
   Wheel: 00
   ```

2. **Calculate checksum**: Sum all bytes % 256

3. **Send complete command**:
   ```
   57 AB 00 04 07 02 01 F4 01 2C 01 00 [checksum]
   ```

4. **Send release command** (button = 0x00):
   ```
   57 AB 00 04 07 02 00 F4 01 2C 01 00 [checksum]
   ```

## Troubleshooting Tips

1. **Mouse not moving**:
   - Verify CH9329 mode is 0x82
   - Check target USB connection status
   - Ensure proper checksum calculation
   - Verify coordinates are within valid range

2. **Connection issues**:
   - Try factory reset (RTS low for 4 seconds for v1.9 hardware)
   - Use CMD_SET_DEFAULT_CFG for v1.9.1 hardware
   - Check both 115200 and 9600 baudrates

3. **Debug suggestions**:
   - Enable serial port debug dialog to monitor commands
   - Check response status codes
   - Verify VID:PID detection
   - Monitor target connection status via CMD_GET_INFO