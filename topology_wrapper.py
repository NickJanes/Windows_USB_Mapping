import ctypes
from ctypes import Structure, c_int, c_char, c_ushort
import json

# Define the structure matching our C struct
class USBDeviceInfo(Structure):
    _fields_ = [
        ("hubIndex", c_int),
        ("portNumber", c_int),
        ("deviceDesc", c_char * 256),
        ("devicePath", c_char * 512),
        ("isHub", c_int),
        ("speed", c_int),
        ("vendorId", c_ushort),
        ("productId", c_ushort),
    ]

class USBTopologyMapper:
    def __init__(self, dll_path="usb_mapper.dll"):
        """Initialize the USB mapper by loading the DLL"""
        import os
        
        # Try to find the DLL
        possible_paths = [
            dll_path,  # As provided
            os.path.join(os.path.dirname(__file__), dll_path),  # Same dir as script
            os.path.join(os.getcwd(), dll_path),  # Current working directory
            os.path.abspath(dll_path),  # Absolute path
        ]
        
        dll_found = None
        for path in possible_paths:
            if os.path.exists(path):
                dll_found = path
                print(f"Found DLL at: {path}")
                break
        
        if not dll_found:
            raise RuntimeError(
                f"Could not find {dll_path}!\n"
                f"Searched in:\n" + "\n".join(f"  - {p}" for p in possible_paths) +
                f"\n\nCurrent directory: {os.getcwd()}"
            )
        
        try:
            self.dll = ctypes.WinDLL(dll_found)
        except OSError as e:
            raise RuntimeError(
                f"Failed to load DLL from {dll_found}\n"
                f"Error: {e}\n\n"
                f"This might be caused by:\n"
                f"  1. Architecture mismatch (32-bit Python with 64-bit DLL or vice versa)\n"
                f"  2. Missing Visual C++ Runtime\n"
                f"  3. Missing dependencies\n\n"
                f"Your Python: {ctypes.sizeof(ctypes.c_voidp) * 8}-bit"
            )
        
        # Define function signatures
        self.dll.EnumerateUSBDevices.argtypes = []
        self.dll.EnumerateUSBDevices.restype = c_int
        
        self.dll.GetDeviceCount.argtypes = []
        self.dll.GetDeviceCount.restype = c_int
        
        self.dll.GetDeviceInfo.argtypes = [c_int, ctypes.POINTER(USBDeviceInfo)]
        self.dll.GetDeviceInfo.restype = c_int
    
    def enumerate(self):
        """Enumerate USB devices and return as Python list"""
        # Call the enumeration function
        count = self.dll.EnumerateUSBDevices()
        
        if count < 0:
            raise RuntimeError("Failed to enumerate USB devices")
        
        devices = []
        
        # Retrieve each device
        for i in range(count):
            info = USBDeviceInfo()
            if self.dll.GetDeviceInfo(i, ctypes.byref(info)):
                device = {
                    "hub_index": info.hubIndex,
                    "port_number": info.portNumber,
                    "description": info.deviceDesc.decode('utf-8', errors='ignore'),
                    "device_path": info.devicePath.decode('utf-8', errors='ignore'),
                    "is_hub": bool(info.isHub),
                    "speed": self._speed_to_string(info.speed),
                    "vendor_id": f"0x{info.vendorId:04X}",
                    "product_id": f"0x{info.productId:04X}",
                }
                devices.append(device)
        
        return devices
    
    def _speed_to_string(self, speed):
        """Convert speed code to readable string"""
        speed_map = {
            0: "Low Speed (1.5 Mbps)",
            1: "Full Speed (12 Mbps)",
            2: "High Speed (480 Mbps)",
            3: "Super Speed (5 Gbps)",
            -1: "Unknown"
        }
        return speed_map.get(speed, "Unknown")
    
    def print_topology(self):
        """Print the USB topology in a readable format"""
        devices = self.enumerate()
        
        if not devices:
            print("No USB devices found.")
            return
        
        print("=" * 70)
        print("USB TOPOLOGY MAP")
        print("=" * 70)
        
        # Group by hub
        hubs = {}
        for device in devices:
            hub_idx = device["hub_index"]
            if hub_idx not in hubs:
                hubs[hub_idx] = []
            hubs[hub_idx].append(device)
        
        for hub_idx in sorted(hubs.keys()):
            print(f"\n[HUB {hub_idx}]")
            for device in sorted(hubs[hub_idx], key=lambda x: x["port_number"]):
                print(f"  Port {device['port_number']}:")
                print(f"    Description: {device['description']}")
                print(f"    Speed: {device['speed']}")
                print(f"    VID: {device['vendor_id']}, PID: {device['product_id']}")
                print(f"    Type: {'Hub (cascaded)' if device['is_hub'] else 'Device'}")
        
        print("\n" + "=" * 70)
    
    def to_json(self, filepath=None):
        """Export topology to JSON"""
        devices = self.enumerate()
        json_data = json.dumps(devices, indent=2)
        
        if filepath:
            with open(filepath, 'w') as f:
                f.write(json_data)
            print(f"Topology saved to {filepath}")
        else:
            print(json_data)
        
        return json_data


def main():
    try:
        mapper = USBTopologyMapper("usb_mapper.dll")
        
        print("Enumerating USB devices...\n")
        mapper.print_topology()
        
        # Optionally save to JSON
        print("\nSaving to JSON...")
        mapper.to_json("usb_topology.json")
        
    except Exception as e:
        print(f"Error: {e}")
        print("\nMake sure usb_mapper.dll is in the same directory!")


if __name__ == "__main__":
    main()