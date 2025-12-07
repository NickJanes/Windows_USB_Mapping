# USB Topology Mapper - Complete Technical Guide

## Table of Contents
1. [Windows Device Architecture](#windows-device-architecture)
2. [SetupAPI Deep Dive](#setupapi-deep-dive)
3. [USB Topology C Implementation](#usb-topology-c-implementation)
4. [Python ctypes Bridge](#python-ctypes-bridge)
5. [DLL Architecture & Deployment](#dll-architecture--deployment)
6. [Build Tools Explained](#build-tools-explained)
7. [Deployment Strategy](#deployment-strategy)

---

## 1. Windows Device Architecture

### How Windows Manages Devices

Unlike Linux's filesystem-based approach (`/dev`, `/sys`), Windows uses a **layered driver model**:

```
Application Layer (Your Python app)
        ↓
Win32 API (SetupAPI, DeviceIoControl)
        ↓
I/O Manager (kernel)
        ↓
Device Drivers (USB hub driver, device drivers)
        ↓
Hardware (USB controllers, hubs, devices)
```

### The Windows Device Tree

Windows maintains a hierarchical device tree similar to Linux's sysfs, but it's not exposed as files. Instead, you query it through APIs:

- **Device Instance IDs**: Unique identifiers like `USB\VID_046D&PID_C52B\5&2A8D8F8&0&3`
- **Device Classes**: GUID-based categories (USB hubs, keyboards, storage, etc.)
- **Device Interfaces**: How applications communicate with devices

**Key Concept**: Every USB device appears in multiple places:
- As a device node (with properties like manufacturer, description)
- As a device interface (with a path you can open like `\\?\usb#vid_...`)
- In the USB topology tree (port number, parent hub)

---

## 2. SetupAPI Deep Dive

### What is SetupAPI?

SetupAPI (`setupapi.dll`) is Windows' device configuration and enumeration API. It's the official way to:
- Enumerate devices
- Query device properties
- Get device interface paths
- Install/manage drivers

Think of it as the Windows equivalent of udev on Linux.

### Key SetupAPI Functions We Use

#### `SetupDiGetClassDevs()`
```c
HDEVINFO deviceInfoSet = SetupDiGetClassDevs(
    &GUID_DEVINTERFACE_USB_HUB,  // Only USB hubs
    NULL,                          // No enumerator filter
    NULL,                          // No window handle
    DIGCF_PRESENT | DIGCF_DEVICEINTERFACE  // Only present devices with interfaces
);
```

**What it does**: Returns a "device information set" - essentially a handle to a list of devices matching your criteria.

**The GUID**: `GUID_DEVINTERFACE_USB_HUB` is defined in `usbiodef.h` and identifies the USB hub device class. Windows has GUIDs for every device class.

#### `SetupDiEnumDeviceInterfaces()`
```c
while (SetupDiEnumDeviceInterfaces(
    deviceInfoSet,              // The set we got above
    NULL,                       // Not filtering by device
    &GUID_DEVINTERFACE_USB_HUB,
    index++,                    // Enumerate by index
    &deviceInterfaceData        // Output: interface info
)) {
    // Process each hub...
}
```

**What it does**: Iterates through each device in the set, returning information about its interface.

#### `SetupDiGetDeviceInterfaceDetail()`
```c
SetupDiGetDeviceInterfaceDetail(
    deviceInfoSet,
    &deviceInterfaceData,
    deviceInterfaceDetailData,  // Output: the device path
    requiredSize,
    NULL,
    &deviceInfoData            // Output: device instance data
);
```

**What it does**: Gets the **device path** - the string you need to open the device with `CreateFile()`.

Example path: `\\?\usb#root_hub30#4&1baab4f&0#{f18a0e88-c30c-11d0-8815-00a0c906bed8}`

This is similar to `/dev/bus/usb/001/002` on Linux, but more verbose.

#### `SetupDiGetDeviceRegistryProperty()`
```c
SetupDiGetDeviceRegistryPropertyA(
    deviceInfoSet,
    deviceInfoData,
    SPDRP_DEVICEDESC,  // Property to query (description, manufacturer, etc.)
    &dataType,
    buffer,
    bufferSize,
    &requiredSize
);
```

**What it does**: Reads device properties stored in the Windows Registry. Each device has metadata like:
- `SPDRP_DEVICEDESC`: Friendly name ("USB Root Hub")
- `SPDRP_HARDWAREID`: Hardware IDs (VID/PID)
- `SPDRP_MFG`: Manufacturer
- `SPDRP_DRIVER`: Driver info

---

## 3. USB Topology C Implementation

### The USB IOCTLs (I/O Control Codes)

Once we have a hub's device path, we need to **talk directly to the USB hub driver** to get topology info. This is done via `DeviceIoControl()` with USB-specific IOCTL codes.

#### Opening the Hub Device
```c
HANDLE hHub = CreateFileA(
    devicePath,
    GENERIC_WRITE | GENERIC_READ,
    FILE_SHARE_WRITE | FILE_SHARE_READ,
    NULL,
    OPEN_EXISTING,
    0,
    NULL
);
```

**What this does**: Opens a handle to the hub, similar to `open()` on Linux. This isn't opening a file - it's opening a communication channel to the USB hub driver.

#### Getting Hub Information
```c
USB_NODE_INFORMATION nodeInfo;
DeviceIoControl(
    hHub,
    IOCTL_USB_GET_NODE_INFORMATION,  // Command code
    &nodeInfo,                        // Input buffer
    sizeof(USB_NODE_INFORMATION),
    &nodeInfo,                        // Output buffer (same struct)
    sizeof(USB_NODE_INFORMATION),
    &bytesReturned,
    NULL
);
```

**What IOCTL_USB_GET_NODE_INFORMATION does**: Sends a command to the USB hub driver saying "tell me about yourself". The driver fills in `nodeInfo` with:
- Number of ports
- Hub type (root hub, external hub)
- Power characteristics
- USB version support

**Analogy**: Like sending an ioctl command to a device driver in Linux (`ioctl(fd, COMMAND, &data)`).

#### Getting Per-Port Information
```c
USB_NODE_CONNECTION_INFORMATION_EX connInfo;
connInfo.ConnectionIndex = portNumber;  // Which port to query

DeviceIoControl(
    hHub,
    IOCTL_USB_GET_NODE_CONNECTION_INFORMATION_EX,
    &connInfo,
    sizeof(USB_NODE_CONNECTION_INFORMATION_EX),
    &connInfo,
    sizeof(USB_NODE_CONNECTION_INFORMATION_EX),
    &bytesReturned,
    NULL
);
```

**What this does**: Asks the hub "what's connected to port N?" The driver returns:
- Connection status (connected, not connected, disabled)
- Device speed (Low/Full/High/Super Speed)
- Whether it's a hub or device
- USB device descriptor (VID, PID, device class)

**This is the key to topology mapping**: By querying each port on each hub, we can reconstruct the entire USB tree.

### The Device Descriptor

```c
typedef struct _USB_DEVICE_DESCRIPTOR {
    UCHAR bLength;
    UCHAR bDescriptorType;
    USHORT bcdUSB;
    UCHAR bDeviceClass;
    UCHAR bDeviceSubClass;
    UCHAR bDeviceProtocol;
    UCHAR bMaxPacketSize0;
    USHORT idVendor;     // ← VID: Vendor ID
    USHORT idProduct;    // ← PID: Product ID
    USHORT bcdDevice;
    // ... more fields
} USB_DEVICE_DESCRIPTOR;
```

**VID/PID**: Every USB device has these identifiers:
- **VID** (Vendor ID): Assigned by USB-IF to manufacturers (e.g., 0x046D = Logitech)
- **PID** (Product ID): Manufacturer assigns to each product (e.g., 0xC52B = specific mouse model)

This is how Windows (and Linux) identifies what driver to load.

### Why We Need This Low-Level Approach

Python libraries like `pyusb` use **libusb**, which enumerates devices but doesn't expose:
- Physical port numbers
- Hub relationships
- The actual topology tree

To get "Device X is on Hub Y port 3, which is on Root Hub Z port 2", you **must** use these IOCTLs.

---

## 4. Python ctypes Bridge

### What is ctypes?

`ctypes` is Python's Foreign Function Interface (FFI) - it lets Python call C functions in DLLs without writing C extension code.

**The Problem**: Python doesn't understand C structs, pointers, or Windows calling conventions.
**The Solution**: ctypes provides Python classes that map to C types.

### Type Mapping

| C Type | ctypes Type | Example |
|--------|-------------|---------|
| `int` | `c_int` | `ctypes.c_int(42)` |
| `unsigned short` | `c_ushort` | `ctypes.c_ushort(0x046D)` |
| `char[256]` | `c_char * 256` | `(ctypes.c_char * 256)()` |
| `void*` | `c_void_p` | `ctypes.c_void_p()` |

### Defining the C Struct in Python

```python
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
```

**Critical**: The field order, names, and types **must exactly match** the C struct. Python uses this to:
1. Allocate the correct amount of memory
2. Know where each field is in memory
3. Marshal data between Python and C

**Memory Layout Example**:
```
C struct memory:           Python Structure memory:
[4 bytes: hubIndex    ] ←→ [4 bytes: hubIndex    ]
[4 bytes: portNumber  ] ←→ [4 bytes: portNumber  ]
[256 bytes: deviceDesc] ←→ [256 bytes: deviceDesc]
[512 bytes: devicePath] ←→ [512 bytes: devicePath]
...
```

If they don't match, you get memory corruption, crashes, or garbage data.

### Loading the DLL

```python
self.dll = ctypes.WinDLL("usb_mapper.dll")
```

**WinDLL vs CDLL**:
- `WinDLL`: Uses `stdcall` convention (Windows standard, what most Win32 APIs use)
- `CDLL`: Uses `cdecl` convention (C standard, Linux default)

Since we compiled with MinGW on Windows and are calling from Windows, `WinDLL` is correct.

### Declaring Function Signatures

```python
self.dll.EnumerateUSBDevices.argtypes = []
self.dll.EnumerateUSBDevices.restype = c_int
```

**Why this matters**: Python needs to know:
- What arguments the C function expects (so it can convert Python objects)
- What the return type is (so it can convert the result back to Python)

Without this, ctypes makes assumptions that might be wrong, causing crashes.

### Calling the C Function

```python
count = self.dll.EnumerateUSBDevices()
```

**What happens behind the scenes**:
1. Python calls the DLL function (via WinAPI `GetProcAddress`)
2. The C code executes, filling the global `g_devices` array
3. C returns the count as an integer
4. ctypes converts the C int to a Python int

### Passing Structs by Reference

```python
info = USBDeviceInfo()
self.dll.GetDeviceInfo(i, ctypes.byref(info))
```

**`ctypes.byref()`**: Creates a C pointer to the Python Structure. This is like passing `&info` in C.

The C function writes directly into the memory Python allocated for the Structure.

### Converting C Strings to Python

```python
device["description"] = info.deviceDesc.decode('utf-8', errors='ignore')
```

**Why decode?**: 
- C strings are bytes (char arrays)
- Python 3 strings are Unicode
- `decode('utf-8')` converts bytes → str
- `errors='ignore'` handles invalid UTF-8 gracefully

---

## 5. DLL Architecture & Deployment

### What is a DLL?

**DLL (Dynamic Link Library)** is Windows' shared library format, equivalent to `.so` on Linux or `.dylib` on macOS.

**Key Concepts**:

1. **Dynamic vs Static Linking**:
   - **Static**: Code is copied into your executable at compile time (larger binary, no dependencies)
   - **Dynamic**: Code stays in a separate DLL loaded at runtime (smaller binaries, shared between programs)

2. **The PE (Portable Executable) Format**:
   DLLs are PE files containing:
   - **Code section** (`.text`): Your compiled functions
   - **Data section** (`.data`, `.bss`): Global variables
   - **Import table**: Other DLLs this DLL needs
   - **Export table**: Functions this DLL exposes

### How DLLs are Loaded

```
1. Application calls ctypes.WinDLL("usb_mapper.dll")
2. Windows LoadLibrary() searches for DLL:
   - Current directory
   - System directories (System32, etc.)
   - Directories in PATH
3. Windows loads DLL into process memory
4. Windows resolves imports (loads dependent DLLs)
5. DLL initialization code runs (DllMain)
6. Python gets a handle to the DLL
```

### Export/Import Mechanism

In our C code:
```c
__declspec(dllexport) int EnumerateUSBDevices() { ... }
```

**`__declspec(dllexport)`**: Tells the compiler "make this function visible to programs loading this DLL". It adds the function name to the DLL's export table.

In Python:
```python
dll.EnumerateUSBDevices  # Python looks up this name in the export table
```

**Under the hood**: Windows uses `GetProcAddress("EnumerateUSBDevices")` to find the function's memory address in the loaded DLL.

### DLL Dependencies

When we built the DLL, it depends on:
- `KERNEL32.DLL`: Core Windows APIs (CreateFile, etc.)
- `SETUPAPI.DLL`: Device enumeration APIs
- `ntdll.dll`: Low-level NT kernel interface
- MinGW runtime (if not statically linked)

**Static vs Dynamic Linking for MinGW Runtime**:
- Without `-static-libgcc`: DLL needs `libgcc_s_seh-1.dll`, `libwinpthread-1.dll`
- With `-static-libgcc`: Runtime code embedded in your DLL (larger but self-contained)

### Why the Shell Restart Fixed Your Issue

**Windows DLL Loading Cache**: When a process tries to load a DLL and fails, Windows caches the failure:
```
Process memory:
  DLL_CACHE["usb_mapper.dll"] = LOAD_FAILED
```

Even after you rebuilt the DLL successfully, your existing shell process had this cached. Python saw the cached failure and didn't retry.

**New shell = new process = empty cache = successful load.**

---

## 6. Build Tools Explained

### GCC (GNU Compiler Collection)

**What it does**:
1. **Preprocessing**: Expands `#include`, `#define` macros
2. **Compilation**: Converts C → assembly → object code (`.o` files)
3. **Linking**: Combines object files + libraries → final DLL

**Our command breakdown**:
```bash
gcc -Wall -O2 -shared -static-libgcc -o usb_mapper.dll usb_mapper.c -lsetupapi
```

- `-Wall`: Enable all warnings (catches potential bugs)
- `-O2`: Optimization level 2 (balance speed/size)
- `-shared`: Create a shared library (DLL) instead of executable
- `-static-libgcc`: Statically link GCC runtime
- `-o usb_mapper.dll`: Output filename
- `usb_mapper.c`: Source file
- `-lsetupapi`: Link against `setupapi.lib` (the `-l` means "library")

**Why `-lsetupapi` must come after source files**: GCC's linker processes arguments left-to-right. It resolves symbols as it goes. If you put `-lsetupapi` first, the linker hasn't seen any code that *uses* setupapi functions yet, so it thinks "I don't need this library" and ignores it.

### MinGW vs MSVC

| Tool | MinGW-w64 | Microsoft Visual C++ (MSVC) |
|------|-----------|----------------------------|
| Compiler | `gcc` | `cl.exe` |
| Linker | `ld` | `link.exe` |
| Philosophy | Unix-like, open source | Windows-native, proprietary |
| Runtime | GCC runtime (libgcc) | MSVC runtime (msvcrt.dll) |
| Pragma support | Ignores `#pragma comment` | Uses `#pragma comment` |

**Why we used MinGW**: Free, integrates well with MSYS2, produces self-contained DLLs.

### Make

**What Make does**: Automates building by defining dependencies and rules.

```makefile
usb_mapper.dll: usb_mapper.c
    gcc -Wall -O2 -shared -static-libgcc -o usb_mapper.dll usb_mapper.c -lsetupapi
```

**Means**: "To build `usb_mapper.dll`, you need `usb_mapper.c`. If the C file is newer than the DLL (or DLL doesn't exist), run this command."

**Why it's useful**:
- Type `make` instead of remembering long gcc commands
- Only rebuilds what changed
- Defines common tasks (build, clean, test)

### ldd (List Dynamic Dependencies)

```bash
ldd usb_mapper.dll
```

**What it does**: Shows all DLLs your DLL depends on, similar to `ldd` on Linux.

**Output interpretation**:
```
KERNEL32.DLL => /c/WINDOWS/System32/KERNEL32.DLL (0x7ffe05a50000)
```
- First part: DLL name
- `=>`: "is found at"
- Path: Where the DLL is located
- Address: Where it's loaded in memory

If you see `=> not found`, that DLL is missing and your DLL won't load.

---

## 7. Deployment Strategy

### Option 1: Ship Source Code

**Pros**:
- Recipients can rebuild for their system
- Can modify/audit code
- No binary compatibility issues
- Smaller download

**Cons**:
- Recipients need GCC, Make, headers
- More complex setup
- Build errors if their environment differs
- Intellectual property exposed (if that matters)

**When to use**: Open source projects, technical teams, custom deployments

### Option 2: Ship the DLL

**Pros**:
- Simple "plug and play"
- No build tools required
- Faster to deploy
- Protects source code (if desired)

**Cons**:
- Architecture-specific (need 32-bit and 64-bit versions)
- Windows version compatibility concerns
- Recipients can't modify/fix bugs
- Larger if bundling dependencies

**When to use**: Production deployments, non-technical users, commercial software

### Option 3: Ship Python Wheel with Compiled Extension

Build your DLL as a Python extension module:

```python
# setup.py approach
from setuptools import setup, Extension

setup(
    name="usb_topology_mapper",
    ext_modules=[
        Extension("usb_mapper", ["usb_mapper.c"], libraries=["setupapi"])
    ]
)
```

**Then install via**:
```bash
pip install .
```

**Pros**:
- Integrates with Python packaging
- Can distribute via PyPI
- pip handles dependencies
- Cross-platform support possible

**Cons**:
- More complex build setup
- Requires Python dev headers

### Recommended Approach for Your Team

**For internal team use**:
```
Repository structure:
/src
  - usb_mapper.c
  - usb_topology.py
/bin
  - usb_mapper.dll (pre-built, 64-bit)
  - usb_mapper_x86.dll (pre-built, 32-bit, if needed)
Makefile
README.md
requirements.txt (empty for now, or add pyusb if you use it later)
```

**Provide**:
1. **Pre-built DLL** for quick start
2. **Source + Makefile** for those who want to rebuild
3. **Python script** that auto-detects and loads correct DLL

**In README.md, document**:
- Prerequisites (Python 3.x, 64-bit)
- Quick start (just run the Python script)
- How to rebuild (make)
- Troubleshooting (common issues)

### Cross-Platform Considerations

If you ever need Linux support:
- Linux equivalent: `libudev` for device enumeration, reading sysfs for topology
- macOS equivalent: IOKit framework
- Structure your Python code with platform detection:

```python
if sys.platform == 'win32':
    from usb_mapper_windows import USBTopologyMapper
elif sys.platform == 'linux':
    from usb_mapper_linux import USBTopologyMapper
```

### Security Considerations

**DLL Hijacking Risk**: If you load DLLs by name without specifying full paths, malicious DLLs in the current directory could be loaded instead.

**Mitigation**:
```python
import os
dll_path = os.path.abspath(os.path.join(os.path.dirname(__file__), "usb_mapper.dll"))
self.dll = ctypes.WinDLL(dll_path)
```

**Code signing** (advanced): For production software, sign your DLL with a certificate so Windows trusts it.

---

## Summary: What to Tell Your Team

### The Problem
"We need to map USB device topology on Windows - which hub, which port. Standard Python USB libraries don't expose this information."

### The Solution
"We use Windows SetupAPI and USB IOCTLs to query the USB hub driver directly. This requires C code because Python doesn't have bindings for these low-level APIs."

### The Architecture
"We built a C DLL that handles Windows API calls, then use Python's ctypes to call it. This keeps complex Windows code in C (fast, direct) while keeping our application logic in Python (easy, maintainable)."

### The Deliverable
"For the team, we provide:
1. A ready-to-use DLL (no compilation needed)
2. Python wrapper for easy integration
3. Source code and build instructions for transparency

Just run the Python script - it works out of the box."

### Why This Approach
- **Performance**: C code is fast, direct hardware access
- **Maintainability**: Python for business logic, C only for what's necessary
- **Portability**: Could extend to Linux/Mac with platform-specific modules
- **Standard**: Uses official Windows APIs (SetupAPI is how Microsoft intends this to be done)

### Future Enhancements
- Recursive hub traversal (map cascaded hubs)
- Real-time monitoring (detect plug/unplug events)
- Device friendly names (cross-reference with registry)
- Export to formats (JSON, CSV, graphical tree)
