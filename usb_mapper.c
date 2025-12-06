#include <windows.h>
#include <setupapi.h>
#include <initguid.h>
#include <usbioctl.h>
#include <usbiodef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Note: For MinGW, link with -lsetupapi in the makefile
// The #pragma comment is only for MSVC

// Maximum devices we'll report
#define MAX_DEVICES 256
#define MAX_PATH_LEN 512
#define MAX_DESC_LEN 256

// Simplified structure for returning to Python
typedef struct {
    int hubIndex;
    int portNumber;
    char deviceDesc[MAX_DESC_LEN];
    char devicePath[MAX_PATH_LEN];
    int isHub;
    int speed;  // 0=Low, 1=Full, 2=High, 3=Super
    unsigned short vendorId;
    unsigned short productId;
} USBDeviceInfo;

// Global array to store results (simple approach for DLL)
static USBDeviceInfo g_devices[MAX_DEVICES];
static int g_deviceCount = 0;

// Function to get device property string
BOOL GetDeviceProperty(HDEVINFO deviceInfoSet, PSP_DEVINFO_DATA deviceInfoData, 
                       DWORD property, char* buffer, DWORD bufferSize) {
    DWORD dataType;
    DWORD requiredSize;
    
    if (SetupDiGetDeviceRegistryPropertyA(deviceInfoSet, deviceInfoData, property,
                                          &dataType, (PBYTE)buffer, bufferSize, 
                                          &requiredSize)) {
        return TRUE;
    }
    return FALSE;
}

// Function to open a device handle
HANDLE OpenDeviceHandle(const char* devicePath) {
    HANDLE hDevice = CreateFileA(
        devicePath,
        GENERIC_WRITE | GENERIC_READ,
        FILE_SHARE_WRITE | FILE_SHARE_READ,
        NULL,
        OPEN_EXISTING,
        0,
        NULL
    );
    return hDevice;
}

// Query USB hub node information
BOOL GetHubNodeInfo(HANDLE hHub, PUSB_NODE_INFORMATION nodeInfo) {
    DWORD bytesReturned;
    return DeviceIoControl(
        hHub,
        IOCTL_USB_GET_NODE_INFORMATION,
        nodeInfo,
        sizeof(USB_NODE_INFORMATION),
        nodeInfo,
        sizeof(USB_NODE_INFORMATION),
        &bytesReturned,
        NULL
    );
}

// Get information about a specific port on a hub
BOOL GetPortConnectorProperties(HANDLE hHub, int portIndex, 
                                PUSB_NODE_CONNECTION_INFORMATION_EX connInfo) {
    DWORD bytesReturned;
    connInfo->ConnectionIndex = portIndex;
    
    return DeviceIoControl(
        hHub,
        IOCTL_USB_GET_NODE_CONNECTION_INFORMATION_EX,
        connInfo,
        sizeof(USB_NODE_CONNECTION_INFORMATION_EX),
        connInfo,
        sizeof(USB_NODE_CONNECTION_INFORMATION_EX),
        &bytesReturned,
        NULL
    );
}

// Main enumeration function - exported to Python
__declspec(dllexport) int EnumerateUSBDevices() {
    HDEVINFO deviceInfoSet;
    SP_DEVICE_INTERFACE_DATA deviceInterfaceData;
    PSP_DEVICE_INTERFACE_DETAIL_DATA_A deviceInterfaceDetailData;
    DWORD requiredSize;
    DWORD hubIndex = 0;
    
    // Reset global state
    g_deviceCount = 0;
    memset(g_devices, 0, sizeof(g_devices));
    
    // Get all USB hub devices
    deviceInfoSet = SetupDiGetClassDevs(
        &GUID_DEVINTERFACE_USB_HUB,
        NULL,
        NULL,
        DIGCF_PRESENT | DIGCF_DEVICEINTERFACE
    );
    
    if (deviceInfoSet == INVALID_HANDLE_VALUE) {
        return -1;
    }
    
    deviceInterfaceData.cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);
    
    // Enumerate each USB hub
    while (SetupDiEnumDeviceInterfaces(deviceInfoSet, NULL, 
                                       &GUID_DEVINTERFACE_USB_HUB,
                                       hubIndex, &deviceInterfaceData)) {
        
        // Get required size for device path
        SetupDiGetDeviceInterfaceDetailA(deviceInfoSet, &deviceInterfaceData,
                                         NULL, 0, &requiredSize, NULL);
        
        deviceInterfaceDetailData = (PSP_DEVICE_INTERFACE_DETAIL_DATA_A)
                                    malloc(requiredSize);
        deviceInterfaceDetailData->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_A);
        
        SP_DEVINFO_DATA deviceInfoData;
        deviceInfoData.cbSize = sizeof(SP_DEVINFO_DATA);
        
        // Get device path
        if (SetupDiGetDeviceInterfaceDetailA(deviceInfoSet, &deviceInterfaceData,
                                            deviceInterfaceDetailData, requiredSize,
                                            NULL, &deviceInfoData)) {
            
            // Open the hub to query its ports
            HANDLE hHub = OpenDeviceHandle(deviceInterfaceDetailData->DevicePath);
            if (hHub != INVALID_HANDLE_VALUE) {
                USB_NODE_INFORMATION nodeInfo;
                ZeroMemory(&nodeInfo, sizeof(nodeInfo));
                
                if (GetHubNodeInfo(hHub, &nodeInfo)) {
                    int numPorts = nodeInfo.u.HubInformation.HubDescriptor.bNumberOfPorts;
                    
                    // Check each port
                    for (int port = 1; port <= numPorts; port++) {
                        USB_NODE_CONNECTION_INFORMATION_EX connInfo;
                        ZeroMemory(&connInfo, sizeof(connInfo));
                        
                        if (GetPortConnectorProperties(hHub, port, &connInfo)) {
                            if (connInfo.ConnectionStatus == DeviceConnected) {
                                if (g_deviceCount < MAX_DEVICES) {
                                    USBDeviceInfo* dev = &g_devices[g_deviceCount];
                                    
                                    dev->hubIndex = hubIndex;
                                    dev->portNumber = port;
                                    dev->isHub = connInfo.DeviceIsHub ? 1 : 0;
                                    dev->vendorId = connInfo.DeviceDescriptor.idVendor;
                                    dev->productId = connInfo.DeviceDescriptor.idProduct;
                                    
                                    // Map speed enum to simpler int
                                    switch (connInfo.Speed) {
                                        case UsbLowSpeed:  dev->speed = 0; break;
                                        case UsbFullSpeed: dev->speed = 1; break;
                                        case UsbHighSpeed: dev->speed = 2; break;
                                        case UsbSuperSpeed: dev->speed = 3; break;
                                        default: dev->speed = -1;
                                    }
                                    
                                    // Get device description
                                    char hubDesc[256] = {0};
                                    GetDeviceProperty(deviceInfoSet, &deviceInfoData, 
                                                    SPDRP_DEVICEDESC, hubDesc, sizeof(hubDesc));
                                    snprintf(dev->deviceDesc, MAX_DESC_LEN, 
                                            "Hub: %s, Port: %d", hubDesc, port);
                                    
                                    strncpy(dev->devicePath, deviceInterfaceDetailData->DevicePath, 
                                           MAX_PATH_LEN - 1);
                                    
                                    g_deviceCount++;
                                }
                            }
                        }
                    }
                }
                
                CloseHandle(hHub);
            }
        }
        
        free(deviceInterfaceDetailData);
        hubIndex++;
    }
    
    SetupDiDestroyDeviceInfoList(deviceInfoSet);
    
    return g_deviceCount;
}

// Get device info by index - exported to Python
__declspec(dllexport) int GetDeviceInfo(int index, USBDeviceInfo* outInfo) {
    if (index < 0 || index >= g_deviceCount || outInfo == NULL) {
        return 0;
    }
    
    memcpy(outInfo, &g_devices[index], sizeof(USBDeviceInfo));
    return 1;
}

// Get total device count - exported to Python
__declspec(dllexport) int GetDeviceCount() {
    return g_deviceCount;
}