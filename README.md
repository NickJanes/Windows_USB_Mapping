# USB Topology Mapper

Map USB device connections showing hub relationships and port assignments on Windows systems.

## What It Does

Maps the physical USB topology of your system, showing:
- Which devices are connected to which hubs
- Physical port numbers for each device
- Device speed (USB 1.1, 2.0, 3.0, etc.)
- Vendor/Product IDs (VID/PID)
- Cascaded hub structures

**Why this tool?** Standard Python USB libraries (`pyusb`, `pywinusb`) enumerate devices but don't expose the physical topology - you can't tell which hub or port a device is on. This tool uses Windows SetupAPI and USB IOCTLs to get that information.

## Quick Start

### Prerequisites
- Windows 10/11
- Python 3.8+ (64-bit)

### Usage

```bash
# Clone the repository
git clone git@github.com:NickJanes/Windows_USB_Mapping.git
cd Windows_USB_Mapping

# Run the mapper
python usb_topology.py
```

The pre-built `usb_mapper.dll` is included - no compilation needed.

### Output Example

```
======================================================================
USB TOPOLOGY MAP
======================================================================

[HUB 0]
  Port 1:
    Description: Hub: USB Root Hub (USB 3.0), Port: 1
    Speed: High Speed (480 Mbps)
    VID: 0x046D, PID: 0xC52B
    Type: Device

  Port 3:
    Description: Hub: USB Root Hub (USB 3.0), Port: 3
    Speed: Super Speed (5 Gbps)
    VID: 0x0000, PID: 0x0000
    Type: Hub (cascaded)
```

Export to JSON:
```python
from usb_topology import USBTopologyMapper

mapper = USBTopologyMapper()
mapper.to_json("topology.json")
```

## Building from Source

### Prerequisites
- MSYS2 with MinGW64
- GCC compiler
- Make

### Compile the DLL

```bash
# Using Make
make clean
make

# Or manually with GCC
gcc -Wall -O2 -shared -static-libgcc -o usb_mapper.dll usb_mapper.c -lsetupapi
```

### Verify Build

```bash
# Check dependencies
ldd usb_mapper.dll

# Test
make test
```

## Architecture

```
┌─────────────────┐
│  Python App     │  Application logic, data processing
│  usb_topology.py│
└────────┬────────┘
         │ ctypes
         ▼
┌─────────────────┐
│  usb_mapper.dll │  Windows API calls (C)
└────────┬────────┘
         │ Win32 API
         ▼
┌─────────────────┐
│  SetupAPI       │  Device enumeration
│  USB IOCTLs     │  Hub/port queries
└────────┬────────┘
         │
         ▼
┌─────────────────┐
│  USB Hardware   │  Hubs, devices, controllers
└─────────────────┘
```

**Why C + Python?**
- **C DLL**: Direct access to Windows APIs that Python doesn't expose
- **Python**: Easy scripting, data processing, and integration

## Project Structure

```
Windows_USB_Mapping/
├── src/
│   ├── usb_mapper.c          # C implementation (Windows APIs)
│   └── usb_topology.py       # Python wrapper (ctypes)
├── bin/
│   └── usb_mapper.dll        # Pre-built DLL (64-bit)
├── Makefile                  # Build automation
├── README.md                 # This file
└── docs/
    └── TECHNICAL_GUIDE.md    # Deep dive documentation
```

## Troubleshooting

### "Could not find usb_mapper.dll"
- Ensure `usb_mapper.dll` is in the same directory as `usb_topology.py`
- Or specify full path: `USBTopologyMapper("C:/path/to/usb_mapper.dll")`

### "Failed to load DLL" or "Architecture mismatch"
- Verify Python is 64-bit: `python -c "import ctypes; print(ctypes.sizeof(ctypes.c_voidp) * 8)"`
- The included DLL is 64-bit only
- For 32-bit Python, rebuild with 32-bit MinGW

### DLL loads but shows no devices
- Run as Administrator (some USB hubs require elevated privileges)
- Check Device Manager to verify USB hubs are present
- Try unplugging/replugging devices

### After rebuilding, changes don't take effect
- Close all Python shells/processes before rebuilding
- Windows caches loaded DLLs - you need a fresh process

## API Reference

```python
from usb_topology import USBTopologyMapper

# Initialize
mapper = USBTopologyMapper()

# Get devices as Python list
devices = mapper.enumerate()
# Returns: [{"hub_index": 0, "port_number": 1, ...}, ...]

# Print formatted topology
mapper.print_topology()

# Export to JSON
mapper.to_json("output.json")
```

## Limitations

- **Windows only** - Uses Windows-specific APIs
- **USB hubs only** - Doesn't enumerate Bluetooth, virtual devices, etc.
- **Snapshot** - Not real-time monitoring (run again to refresh)
- **Permissions** - Some system hubs may require Administrator access

## Future Enhancements

- [ ] Recursive hub traversal for deeply nested hubs
- [ ] Real-time monitoring (detect plug/unplug events)
- [ ] Device friendly names (cross-reference with device manager)
- [ ] Graphical tree visualization
- [ ] Linux/macOS support (using platform-specific APIs)

## Contributing

1. Fork the repository
2. Create a feature branch (`git checkout -b feature/amazing-feature`)
3. Commit your changes (`git commit -m 'Add amazing feature'`)
4. Push to the branch (`git push origin feature/amazing-feature`)
5. Open a Pull Request

## Technical Details

For in-depth information about how this works, see [TECHNICAL_GUIDE.md](docs/TECHNICAL_GUIDE.md):
- Windows device architecture
- SetupAPI and USB IOCTLs explained
- DLL compilation and ctypes integration
- Deployment strategies

## License

MIT License - see LICENSE file for details

## Author

Nick Janes ([@NickJanes](https://github.com/NickJanes))

## Acknowledgments

- Built using Windows SetupAPI (setupapi.dll)
- USB enumeration via IOCTL_USB_GET_NODE_CONNECTION_INFORMATION_EX
- Compiled with MinGW-w64 GCC
