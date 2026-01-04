#include <windows.h>

#include <mutex>
#include <string>

#include "IPlatform.h"
#include "IDeviceManager.h"
#include "ICPUEx.h"
#include "Utility.hpp"
#include "rmmon_wrapper.h"

typedef IPlatform& (__stdcall* GetPlatformFunc)();
extern "C" IMAGE_DOS_HEADER __ImageBase;

static std::mutex g_mu;
static HMODULE g_platform_dll = nullptr;
static IPlatform* g_platform = nullptr;
static ICPUEx* g_cpu = nullptr;

static int fail(int code) {
    return code;
}

static std::wstring module_dir_path() {
    std::wstring path(MAX_PATH, L'\0');
    DWORD len = GetModuleFileNameW(reinterpret_cast<HMODULE>(&__ImageBase), path.data(), MAX_PATH);
    if (len == 0 || len >= MAX_PATH) {
        return L"";
    }
    path.resize(len);
    size_t pos = path.find_last_of(L"\\/");
    if (pos == std::wstring::npos) {
        return L"";
    }
    return path.substr(0, pos + 1);
}

int rm_init(void) {
    std::lock_guard<std::mutex> lk(g_mu);
    if (g_platform && g_cpu) {
        return 0;
    }

    std::wstring dllPath;
    std::wstring moduleDir = module_dir_path();
    if (!moduleDir.empty()) {
        dllPath = moduleDir + L"Platform.dll";
        g_platform_dll = LoadLibraryExW(dllPath.c_str(), NULL, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR);
    }

    if (!g_platform_dll) {
    std::wstring installPath;
    DWORD dummy = 0;
    if (!g_GetRegistryValue(HKEY_LOCAL_MACHINE,
                            AMDRM_Monitoring_SDK_REGISTRY_PATH,
                            L"InstallationPath",
                            installPath,
                            dummy)) {
        return fail(1001);
    }

    dllPath = installPath + L"bin\\Platform.dll";
    g_platform_dll = LoadLibraryExW(dllPath.c_str(), NULL, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR);
    if (!g_platform_dll) {
        return fail(1004);
    }
    }

    auto platformFunc = reinterpret_cast<GetPlatformFunc>(GetProcAddress(g_platform_dll, "GetPlatform"));
    if (!platformFunc) {
        return fail(1005);
    }

    IPlatform& platformRef = platformFunc();
    if (!platformRef.Init()) {
        return fail(1006);
    }

    IDeviceManager& dm = platformRef.GetIDeviceManager();
    g_cpu = reinterpret_cast<ICPUEx*>(dm.GetDevice(dtCPU, 0));
    if (!g_cpu) {
        return fail(1007);
    }

    g_platform = &platformRef;
    return 0;
}

int rm_read(struct RmSnapshot* out) {
    if (!out) {
        return fail(2001);
    }

    std::lock_guard<std::mutex> lk(g_mu);
    if (!g_platform || !g_cpu) {
        return fail(2002);
    }

    CPUParameters st{};
    int ret = g_cpu->GetCPUParameters(st);
    if (ret != 0) {
        return fail(2003);
    }

    float ppt = st.fPPTValue;
    float temp = static_cast<float>(st.dTemperature);

    double usageSum = 0.0;
    int usageCnt = 0;

    if (st.stFreqData.uLength > 0 && st.stFreqData.dState) {
        for (unsigned int i = 0; i < st.stFreqData.uLength; i++) {
            if (st.stFreqData.dCurrentFreq && st.stFreqData.dCurrentFreq[i] == 0.0) {
                continue;
            }

            double v = st.stFreqData.dState[i];
            if (v < 0.0) {
                v = 0.0;
            }
            if (v > 100.0) {
                v = 100.0;
            }
            usageSum += v;
            usageCnt++;
        }
    }

    float usage = 0.0f;
    if (usageCnt > 0) {
        usage = static_cast<float>(usageSum / usageCnt);
    }

    out->ppt_power_w = ppt;
    out->temp_c = temp;
    out->usage_pct = usage;
    out->flags = 1;
    return 0;
}

void rm_shutdown(void) {
    std::lock_guard<std::mutex> lk(g_mu);

    if (g_platform) {
        g_platform->UnInit();
        g_platform = nullptr;
        g_cpu = nullptr;
    }
    if (g_platform_dll) {
        FreeLibrary(g_platform_dll);
        g_platform_dll = nullptr;
    }
}
