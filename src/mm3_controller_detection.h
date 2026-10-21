#ifndef MM3_CONTROLLER_DETECTION_H
#define MM3_CONTROLLER_DETECTION_H

#include <windows.h>
#include <stdlib.h>

/* Physical HID presence is separate from the XInput path used by the game.
 * VID/PIDs: https://github.com/libsdl-org/SDL/blob/main/src/joystick/controller_list.h
 * The caller caches this result; enumeration does not need raw-input registration. */
static int mm3_dualsense_connected(void)
{
    for (int attempt = 0; attempt < 3; ++attempt) {
        RAWINPUTDEVICELIST *devices;
        UINT capacity = 0, listed;
        int connected = 0;
        if (GetRawInputDeviceList(NULL, &capacity, sizeof *devices) == (UINT)-1 || !capacity)
            return 0;
        if ((size_t)capacity > (size_t)-1 / sizeof *devices) return 0;
        devices = (RAWINPUTDEVICELIST *)calloc(capacity, sizeof *devices);
        if (!devices) return 0;
        listed = GetRawInputDeviceList(devices, &capacity, sizeof *devices);
        if (listed == (UINT)-1) {
            DWORD error = GetLastError();
            free(devices);
            /* A device can arrive between the count and list calls. */
            if (error == ERROR_INSUFFICIENT_BUFFER) continue;
            return 0;
        }
        for (UINT i = 0; i < listed; ++i) {
            RID_DEVICE_INFO info = {0};
            UINT size = sizeof info;
            if (devices[i].dwType != RIM_TYPEHID) continue;
            info.cbSize = size;
            /* A disconnected device may disappear before its info is read. */
            if (GetRawInputDeviceInfoA(devices[i].hDevice, RIDI_DEVICEINFO, &info, &size) == (UINT)-1 ||
                info.dwType != RIM_TYPEHID) continue;
            if (info.hid.dwVendorId == 0x054c &&
                (info.hid.dwProductId == 0x0ce6 || info.hid.dwProductId == 0x0df2) &&
                info.hid.usUsagePage == 1 &&
                (info.hid.usUsage == 4 || info.hid.usUsage == 5))
                ++connected;
        }
        free(devices);
        return connected;
    }
    return 0;
}

#endif
