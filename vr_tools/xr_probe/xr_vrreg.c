// GammaVR test-rig helper (wine/Proton only, test prefix only).
// Proton's OpenXR bridge (wineopenxr) refuses to start unless HKCU\Software\Wine\VR holds "state" = 1 and the
// Vulkan extension lists. In a Steam launch, Proton's steam.exe + vrclient write them, but only when an OpenVR
// client library loads. This helper does the OpenXR half of that work without OpenVR:
// create the key, state 0, call wineopenxr's own wineopenxr_init_registry export, then state 1 (or -1 on failure).
#include <windows.h>
#include <stdio.h>

int main(void)
{
    HKEY key; DWORD v = 0; LSTATUS st;
    st = RegCreateKeyExA(HKEY_CURRENT_USER, "Software\\Wine\\VR", 0, NULL, REG_OPTION_VOLATILE, KEY_ALL_ACCESS, NULL, &key, NULL);
    if (st) { printf("[xr_vrreg] create key failed %ld\n", (long)st); return 2; }
    RegSetValueExA(key, "state", 0, REG_DWORD, (BYTE*)&v, sizeof(v));
    HMODULE xr = LoadLibraryA("wineopenxr");
    if (!xr) { printf("[xr_vrreg] LoadLibrary wineopenxr failed %lu\n", GetLastError()); v = (DWORD)-1; RegSetValueExA(key, "state", 0, REG_DWORD, (BYTE*)&v, sizeof(v)); return 3; }
    BOOL (__cdecl *init)(void) = (BOOL (__cdecl *)(void))GetProcAddress(xr, "wineopenxr_init_registry");
    if (!init) { printf("[xr_vrreg] no wineopenxr_init_registry export\n"); return 4; }
    BOOL ok = init();
    printf("[xr_vrreg] wineopenxr_init_registry returned %d\n", (int)ok);
    char buf[4096]; DWORD sz = sizeof(buf), type = 0;
    if (!RegQueryValueExA(key, "openxr_vulkan_instance_extensions", NULL, &type, (BYTE*)buf, &sz)) printf("[xr_vrreg] instance extensions: %s\n", buf);
    sz = sizeof(buf);
    if (!RegQueryValueExA(key, "openxr_vulkan_device_extensions", NULL, &type, (BYTE*)buf, &sz)) printf("[xr_vrreg] device extensions: %s\n", buf);
    v = ok ? 1 : (DWORD)-1;
    RegSetValueExA(key, "state", 0, REG_DWORD, (BYTE*)&v, sizeof(v));
    printf("[xr_vrreg] state set to %d\n", (int)v);
    RegCloseKey(key);
    return ok ? 0 : 5;
}
