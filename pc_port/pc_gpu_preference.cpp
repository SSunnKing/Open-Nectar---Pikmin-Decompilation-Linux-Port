#include "pc_gpu_preference.h"

#include <cstdio>
#include <cstdlib>

#ifdef __linux__
#include <sys/stat.h>
#endif

const char* const kPcGpuEglVendorPath = "/usr/share/glvnd/egl_vendor.d/10_nvidia.json";

PcGpuDecision pc_gpu_preference_decide(PcGpuEvidence evidence)
{
    PcGpuDecision decision = { 0, 0, 0, "" };

    if (evidence.optedOut) {
        decision.reason = "NECTAR_NO_PRIME is set; leaving the GPU choice alone";
        return decision;
    }
    if (evidence.userAlreadyChose) {
        decision.reason = "the environment already selects a GPU; not overriding it";
        return decision;
    }
    if (!evidence.nvidiaKernelModuleLoaded) {
        decision.reason = "no NVIDIA kernel module; nothing to offload to";
        return decision;
    }

    // The module is loaded, so the GLX vendor it ships with can be asked for.
    // Requesting offload without routing GLX would be half a request: the
    // offload variable alone leaves the Mesa vendor answering, on the Intel GPU.
    decision.requestOffload = 1;
    decision.routeGlx       = 1;

    if (evidence.forceEglRoute && evidence.eglVendorFilePresent) {
        decision.routeEgl = 1;
        decision.reason   = "NVIDIA present; requesting PRIME offload for GLX and EGL "
                            "(NECTAR_PRIME_EGL)";
    } else {
        // GLX is enough for X11/Xwayland. Pinning EGL to NVIDIA exclusively
        // is what produces "Could not get EGL display" on Wayland when the
        // compositor is on the integrated GPU.
        decision.reason = "NVIDIA present; requesting PRIME offload for GLX "
                          "(EGL left to the compositor)";
    }
    return decision;
}

void pc_gpu_preference_apply(void)
{
#ifdef __linux__
    PcGpuEvidence evidence;

    struct stat st;
    evidence.nvidiaKernelModuleLoaded = (stat("/proc/driver/nvidia", &st) == 0);
    evidence.eglVendorFilePresent     = (stat(kPcGpuEglVendorPath, &st) == 0);
    evidence.userAlreadyChose         = (getenv("__NV_PRIME_RENDER_OFFLOAD") != nullptr
                                         || getenv("__GLX_VENDOR_LIBRARY_NAME") != nullptr
                                         || getenv("__EGL_VENDOR_LIBRARY_FILENAMES") != nullptr
                                         || getenv("DRI_PRIME") != nullptr);
    evidence.optedOut                 = (getenv("NECTAR_NO_PRIME") != nullptr);
    evidence.forceEglRoute            = (getenv("NECTAR_PRIME_EGL") != nullptr);

    PcGpuDecision decision = pc_gpu_preference_decide(evidence);

    if (decision.requestOffload) setenv("__NV_PRIME_RENDER_OFFLOAD", "1", 1);
    if (decision.routeGlx)       setenv("__GLX_VENDOR_LIBRARY_NAME", "nvidia", 1);
    if (decision.routeEgl)       setenv("__EGL_VENDOR_LIBRARY_FILENAMES", kPcGpuEglVendorPath, 1);

    printf("[PC Port] GPU preference: %s\n", decision.reason);
#endif
}

void pc_gpu_preference_clear(void)
{
#ifdef __linux__
    unsetenv("__NV_PRIME_RENDER_OFFLOAD");
    unsetenv("__GLX_VENDOR_LIBRARY_NAME");
    unsetenv("__EGL_VENDOR_LIBRARY_FILENAMES");
#endif
}
