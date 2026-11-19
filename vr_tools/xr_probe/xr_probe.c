// GammaVR bridge probe: does a Windows OpenXR program reach a runtime? No graphics, no session.
// Prints every result code. Exit 0 if an instance and a head-mounted system were obtained.
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <openxr/openxr.h>

int main(void)
{
    uint32_t n = 0;
    XrResult r = xrEnumerateInstanceExtensionProperties(NULL, 0, &n, NULL);
    printf("[xr_probe] enumerate extensions: result %d, count %u\n", (int)r, n);
    if (XR_SUCCEEDED(r) && n) {
        XrExtensionProperties* e = (XrExtensionProperties*)calloc(n, sizeof(*e));
        for (uint32_t i = 0; i < n; i++) e[i].type = XR_TYPE_EXTENSION_PROPERTIES;
        r = xrEnumerateInstanceExtensionProperties(NULL, n, &n, e);
        for (uint32_t i = 0; XR_SUCCEEDED(r) && i < n; i++) printf("[xr_probe]   ext %s v%u\n", e[i].extensionName, e[i].extensionVersion);
        free(e);
    }
    XrInstanceCreateInfo ci; memset(&ci, 0, sizeof(ci));
    ci.type = XR_TYPE_INSTANCE_CREATE_INFO;
    strcpy(ci.applicationInfo.applicationName, "gammavr_xr_probe");
    ci.applicationInfo.apiVersion = XR_MAKE_VERSION(1, 0, 0);
    XrInstance inst = XR_NULL_HANDLE;
    r = xrCreateInstance(&ci, &inst);
    printf("[xr_probe] xrCreateInstance: result %d\n", (int)r);
    if (XR_FAILED(r)) return 2;
    XrInstanceProperties ip; memset(&ip, 0, sizeof(ip)); ip.type = XR_TYPE_INSTANCE_PROPERTIES;
    r = xrGetInstanceProperties(inst, &ip);
    printf("[xr_probe] runtime: '%s' version %u.%u.%u (result %d)\n", ip.runtimeName,
           (unsigned)XR_VERSION_MAJOR(ip.runtimeVersion), (unsigned)XR_VERSION_MINOR(ip.runtimeVersion),
           (unsigned)XR_VERSION_PATCH(ip.runtimeVersion), (int)r);
    XrSystemGetInfo si; memset(&si, 0, sizeof(si)); si.type = XR_TYPE_SYSTEM_GET_INFO;
    si.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
    XrSystemId sys = XR_NULL_SYSTEM_ID;
    r = xrGetSystem(inst, &si, &sys);
    printf("[xr_probe] xrGetSystem: result %d, id %llu\n", (int)r, (unsigned long long)sys);
    int ok = XR_SUCCEEDED(r);
    if (ok) {
        XrSystemProperties sp; memset(&sp, 0, sizeof(sp)); sp.type = XR_TYPE_SYSTEM_PROPERTIES;
        r = xrGetSystemProperties(inst, sys, &sp);
        printf("[xr_probe] system: '%s' max %ux%u, orientation %d position %d (result %d)\n", sp.systemName,
               sp.graphicsProperties.maxSwapchainImageWidth, sp.graphicsProperties.maxSwapchainImageHeight,
               (int)sp.trackingProperties.orientationTracking, (int)sp.trackingProperties.positionTracking, (int)r);
    }
    xrDestroyInstance(inst);
    printf("[xr_probe] done, %s\n", ok ? "OK" : "NO SYSTEM");
    return ok ? 0 : 3;
}
