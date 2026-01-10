//===----------------------------------------------------------------------===
//=== Copyright (c) 2023-2025 Advanced Micro Devices, Inc.  All rights reserved.
//
//            Developed by: Advanced Micro Devices, Inc.

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <sddl.h>
#include <Shlobj.h>
#include <intrin.h>
#include <algorithm>
#include <cmath>
#include <new>
#include <string>
#include "ICPUEx.h"
#include "IPlatform.h"
#include "IDeviceManager.h"
#include "IBIOSEx.h"

#include "Utility.hpp"


typedef IPlatform& (__stdcall* GetPlatformFunc)();

enum CPU_PackageType
{
	cptFP5 = 0,
	cptAM5 = 0,
	cptFP7 = 1,
	cptFL1 = 1,
	cptFP8 = 1,
	cptAM4 = 2,
	cptFP7r2 = 2,
	cptAM5_B0 = 3,
	cptSP3 = 4,
	cptFP7_B0 = 4,
	cptFP7R2_B0 = 5,
	cptSP3r2 = 7,
	cptSP6 = 8,
	cptUnknown = 0xF
};

struct MonitoringContext
{
	HMODULE hPlatform = nullptr;
	IPlatform* platform = nullptr;
	ICPUEx* cpu = nullptr;
	IBIOSEx* bios = nullptr;
	bool platformInitialized = false;
};

template <typename FreqData>
const double* GetCurrentFreqPtr(const FreqData& data)
{
	if constexpr (requires { data.dCurrentFreq; })
	{
		return data.dCurrentFreq;
	}
	else if constexpr (requires { data.dFreq; })
	{
		return data.dFreq;
	}
	else
	{
		return nullptr;
	}
}

template <typename FreqData>
const double* GetCurrentTempPtr(const FreqData& data)
{
	if constexpr (requires { data.dCurrentTemp; })
	{
		return data.dCurrentTemp;
	}
	else
	{
		return nullptr;
	}
}

template <typename FreqData>
bool GetResidencyPercent(const FreqData& data, unsigned int idx, double& out_percent)
{
	if constexpr (requires { data.dState; })
	{
		if (!data.dState)
		{
			return false;
		}
		out_percent = data.dState[idx];
		return true;
	}
	else if constexpr (requires { data.bState; })
	{
		if (!data.bState)
		{
			return false;
		}
		out_percent = data.bState[idx] ? 100.0 : 0.0;
		return true;
	}
	else
	{
		(void)idx;
		(void)out_percent;
		return false;
	}
}

static bool TryLoadPlatformFromFile(MonitoringContext& ctx, const std::wstring& dllPath)
{
	if (dllPath.empty())
	{
		return false;
	}

	ctx.hPlatform = LoadLibraryExW(dllPath.c_str(), NULL, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR);
	return ctx.hPlatform != nullptr;
}

static bool TryLoadPlatformFromDir(MonitoringContext& ctx, const std::wstring& dir)
{
	if (dir.empty())
	{
		return false;
	}

	std::wstring base = dir;
	if (!base.empty() && base.back() != L'\\' && base.back() != L'/')
	{
		base += L'\\';
	}

	if (TryLoadPlatformFromFile(ctx, base + L"Platform.dll"))
	{
		return true;
	}

	return TryLoadPlatformFromFile(ctx, base + L"bin\\Platform.dll");
}

void CleanupMonitoringContext(MonitoringContext& ctx)
{
	if (ctx.platform && ctx.platformInitialized)
	{
		ctx.platform->UnInit();
	}
	if (ctx.hPlatform)
	{
		FreeLibrary(ctx.hPlatform);
	}
	ctx = {};
}

bool InitMonitoringContext(MonitoringContext& ctx)
{
	const wchar_t* sdkPath = GetMonitorSdkPath();
	if (!sdkPath || !TryLoadPlatformFromDir(ctx, sdkPath))
	{
		return false;
	}

	if (!ctx.hPlatform)
	{
		return false;
	}

	GetPlatformFunc platformFunc = (GetPlatformFunc)GetProcAddress(ctx.hPlatform, "GetPlatform");
	if (!platformFunc)
	{
		CleanupMonitoringContext(ctx);
		return false;
	}

	ctx.platform = &platformFunc();
	ctx.platformInitialized = ctx.platform->Init();
	if (!ctx.platformInitialized)
	{
		CleanupMonitoringContext(ctx);
		return false;
	}

	IDeviceManager& rDeviceManager = ctx.platform->GetIDeviceManager();
	ctx.cpu = (ICPUEx*)rDeviceManager.GetDevice(dtCPU, 0);
	ctx.bios = (IBIOSEx*)rDeviceManager.GetDevice(dtBIOS, 0);
	if (!ctx.cpu || !ctx.bios)
	{
		CleanupMonitoringContext(ctx);
		return false;
	}

	return true;
}


BOOL IsSupportedProcessor(VOID)
{
	bool retBool = false;
	int CPUInfo[4] = { -1 };
	__cpuid(CPUInfo, 0x80000001);
	unsigned long uCPUID = CPUInfo[0];
	CPU_PackageType pkgType = (CPU_PackageType)((CPUInfo[1] >> 28) & 0x0F);

	switch (pkgType)
	{
	case cptFP5:
		//case cptAM5:
		switch (uCPUID)
		{
		case 0x00810F80:
		case 0x00810F81:
		case 0x00860F00:
		case 0x00860F01:
		case 0x00A50F00:
		case 0x00A50F01:
		case 0x00860F81:
		case 0x00A60F00:
		case 0x00A60F01:
		case 0x00A60F10:
		case 0x00A60F11:
		case 0x00A60F12:
		case 0x00A60F13:
		case 0x00A70F80:
		case 0x00A70F52:

		case 0x00B40F40:
		case 0x00B40F41:
			retBool = true;
			break;
		default:
			break;
		}
		break;

	case cptAM4:
	case cptFP7R2_B0:
		switch (uCPUID)
		{
		case 0x00800F00:
		case 0x00800F10:
		case 0x00800F11:
		case 0x00800F12:

		case 0x00810F10:
		case 0x00810F11:

		case 0x00800F82:
		case 0x00800F83:

		case 0x00870F00:
		case 0x00870F10:

		case 0x00810F80:
		case 0x00810F81:

		case 0x00860F00:
		case 0x00860F01:

		case 0x00A20F00:
		case 0x00A20F10:
		case 0x00A20F12:

		case 0x00A50F00:
		case 0x00A50F01:

		//cptFP7r2
		case 0x00A40F00:
		case 0x00A40F40:
		case 0x00A40F41:

		case 0x00A70F00:
		case 0x00A70F40:
		case 0x00A70F41:
		case 0x00A70F42:
		case 0x00A70F80:
		
		case 0x00A70F52:
		case 0x00A70FC0:
			retBool = true;
			break;
		default:
			break;
		}
		break;

	case cptSP3r2:
		switch (uCPUID)
		{
		case 0x00800F10:
		case 0x00800F11:
		case 0x00800F12:

		case 0x00800F82:
		case 0x00800F83:
		case 0x00830F00:
		case 0x00830F10:
			retBool = true;
			break;
		case 0x00B00F11:
		case 0x00B00F80:
		case 0x00B00F81:
			retBool = true;
			break;
		default:
			break;
		}
		break;

	case cptFP7:
	//case cptFL1:
	//case cptFP8:
	//case cptFP7_B0:
	case cptSP3:
		switch (uCPUID)
		{
		case 0x00A40F00:
		case 0x00A40F40:
		case 0x00A40F41:

		case 0x00A60F11:
		case 0x00A60F12:
			retBool = true;
			break;
		case 0x00A00F80:
		case 0x00A00F82:

		case 0x00A70F00:
		case 0x00A70F40:
		case 0x00A70F41:
		case 0x00A70F42:
		case 0x00A70F80:
		case 0x00A70F52:
		case 0x00A70FC0:
		case 0x00B20F40:

		case 0x00B40F40:
			retBool = true;
			break;

			//STXH - FP11
		case 0x00B70F00:
			retBool = true;
			break;
			//KRK
		case 0x00B60F00:
			retBool = true;
			break;
			//KRK2
		case 0x00B60F80:
			retBool = true;
			break;
		default:
			break;
		}
		break;

	case cptSP6:
		switch (uCPUID)
		{
		case 0x00A10F81:
		case 0x00A10F80:
			retBool = true;
			break;
		}
		break;

	default:
		break;
	}
	return retBool;
}

bool ReadCPUTelemetry(ICPUEx* cpu, double& temperatureC, double& powerW, double& usagePercent)
{
	if (!cpu)
	{
		return false;
	}

	CPUParameters stData = {};
	int iRet = cpu->GetCPUParameters(stData);
	if (iRet)
	{
		return false;
	}

	double occupancy_sum = 0.0;
	unsigned int occupancy_count = 0;
	double max_residency = 0.0;
	const double* freq_ptr = GetCurrentFreqPtr(stData.stFreqData);
	if (stData.stFreqData.uLength)
	{
		for (unsigned int i = 0; i < stData.stFreqData.uLength; ++i)
		{
			if (!freq_ptr || freq_ptr[i] != 0)
			{
				double residency = 0.0;
				if (GetResidencyPercent(stData.stFreqData, i, residency))
				{
					occupancy_sum += residency;
					occupancy_count++;
					max_residency = std::max(max_residency, residency);
				}
			}
		}
	}

	usagePercent = occupancy_count ? (occupancy_sum / occupancy_count) : 0.0;
	if (max_residency > 0.0 && max_residency <= 1.0)
	{
		usagePercent *= 100.0;
	}

	temperatureC = stData.dTemperature;
	if ((!std::isfinite(temperatureC) || temperatureC <= 0.0) && stData.stFreqData.uLength)
	{
		const double* temp_ptr = GetCurrentTempPtr(stData.stFreqData);
		if (temp_ptr)
		{
			double max_temp = 0.0;
			for (unsigned int i = 0; i < stData.stFreqData.uLength; ++i)
			{
				if (temp_ptr[i] > max_temp)
				{
					max_temp = temp_ptr[i];
				}
			}
			if (max_temp > 0.0)
			{
				temperatureC = max_temp;
			}
		}
	}

	powerW = stData.fPPTValue;
	if (!std::isfinite(powerW) || powerW <= 0.0)
	{
		double alt_power = 0.0;
		if (std::isfinite(stData.fVDDCR_VDD_Power) && stData.fVDDCR_VDD_Power > 0.0f)
		{
			alt_power += stData.fVDDCR_VDD_Power;
		}
		if (std::isfinite(stData.fVDDCR_SOC_Power) && stData.fVDDCR_SOC_Power > 0.0f)
		{
			alt_power += stData.fVDDCR_SOC_Power;
		}
		if (alt_power > 0.0)
		{
			powerW = alt_power;
		}
	}
	if (!std::isfinite(powerW) || powerW < 0.0)
	{
		powerW = 0.0;
	}
	return true;
}

enum RMMonitorStatus
{
    RM_STATUS_OK = 0,
    RM_STATUS_INVALID_ARG = 1,
    RM_STATUS_NOT_ADMIN = 2,
    RM_STATUS_UNSUPPORTED_OS = 3,
    RM_STATUS_NOT_AMD = 4,
    RM_STATUS_DRIVER = 5,
    RM_STATUS_UNSUPPORTED_CPU = 6,
    RM_STATUS_ALLOC_FAILED = 7,
    RM_STATUS_SDK_INIT_FAILED = 8,
    RM_STATUS_READ_FAILED = 9
};

struct RMMonitorContext
{
    MonitoringContext ctx = {};
};

extern "C" void rm_monitor_set_sdk_path(const wchar_t* path)
{
    SetMonitorSdkPath(path);
}

extern "C" int rm_monitor_init(RMMonitorContext** out_ctx)
{
    if (!out_ctx)
    {
        return RM_STATUS_INVALID_ARG;
    }

    *out_ctx = nullptr;
    if (!IsUserAnAdmin())
    {
        return RM_STATUS_NOT_ADMIN;
    }
    if (!IsSupportedOS())
    {
        return RM_STATUS_UNSUPPORTED_OS;
    }
    if (!Authentic_AMD())
    {
        return RM_STATUS_NOT_AMD;
    }
    if (QueryDrvService() < 0)
    {
        if (!InstallDriver())
        {
            return RM_STATUS_DRIVER;
        }
    }
    if (!IsSupportedProcessor())
    {
        return RM_STATUS_UNSUPPORTED_CPU;
    }

    RMMonitorContext* wrapper = new (std::nothrow) RMMonitorContext();
    if (!wrapper)
    {
        return RM_STATUS_ALLOC_FAILED;
    }
    if (!InitMonitoringContext(wrapper->ctx))
    {
        delete wrapper;
        return RM_STATUS_SDK_INIT_FAILED;
    }

    *out_ctx = wrapper;
    return RM_STATUS_OK;
}

extern "C" int rm_monitor_read(RMMonitorContext* ctx, double* temperatureC, double* powerW, double* usagePercent)
{
    if (!ctx || !temperatureC || !powerW || !usagePercent)
    {
        return RM_STATUS_INVALID_ARG;
    }

    double temp = 0.0;
    double power = 0.0;
    double usage = 0.0;
    if (!ReadCPUTelemetry(ctx->ctx.cpu, temp, power, usage))
    {
        return RM_STATUS_READ_FAILED;
    }

    *temperatureC = temp;
    *powerW = power;
    *usagePercent = usage;
    return RM_STATUS_OK;
}

extern "C" void rm_monitor_shutdown(RMMonitorContext* ctx)
{
    if (!ctx)
    {
        return;
    }

    CleanupMonitoringContext(ctx->ctx);
    delete ctx;
}

namespace {

constexpr uint32_t kIpcVersion = 1;
constexpr wchar_t kIpcMapName[] = L"Global\\RyzenTelemetryShared";
constexpr wchar_t kIpcOwnerMutexName[] = L"Global\\RyzenTelemetryOwner";
constexpr wchar_t kIpcServiceEventName[] = L"Global\\RyzenTelemetryService";
constexpr wchar_t kIpcSecurityDescriptor[] = L"D:(A;;GA;;;WD)";

enum IpcResult
{
    IPC_OK = 0,
    IPC_NOT_READY = 1,
    IPC_STALE = 2,
    IPC_ERROR = 3
};

struct RMSharedTelemetry
{
    uint32_t version;
    uint32_t size;
    volatile LONG seq;
    uint32_t status;
    uint32_t reserved;
    ULONGLONG timestamp_ms;
    double temperature_c;
    double power_w;
    double usage_percent;
    uint32_t writer_pid;
    uint32_t reserved2;
};

static HANDLE g_ipc_service_event = nullptr;
static HANDLE g_ipc_owner_mutex = nullptr;
static bool g_ipc_owner_held = false;

class SecurityAttributesHolder
{
public:
    SecurityAttributesHolder()
    {
        attrs_.nLength = sizeof(attrs_);
        attrs_.bInheritHandle = FALSE;
        if (ConvertStringSecurityDescriptorToSecurityDescriptorW(
                kIpcSecurityDescriptor,
                SDDL_REVISION_1,
                &descriptor_,
                nullptr))
        {
            attrs_.lpSecurityDescriptor = descriptor_;
        }
        else
        {
            descriptor_ = nullptr;
            attrs_.lpSecurityDescriptor = nullptr;
        }
    }

    ~SecurityAttributesHolder()
    {
        if (descriptor_)
        {
            LocalFree(descriptor_);
            descriptor_ = nullptr;
        }
    }

    SECURITY_ATTRIBUTES* Get()
    {
        return descriptor_ ? &attrs_ : nullptr;
    }

private:
    SECURITY_ATTRIBUTES attrs_{};
    PSECURITY_DESCRIPTOR descriptor_ = nullptr;
};

SECURITY_ATTRIBUTES* GetIpcSecurityAttributes()
{
    static SecurityAttributesHolder holder;
    return holder.Get();
}

RMSharedTelemetry* GetSharedTelemetry()
{
    static HANDLE s_map = nullptr;
    static RMSharedTelemetry* s_view = nullptr;

    if (s_view)
    {
        return s_view;
    }

    HANDLE map = CreateFileMappingW(
        INVALID_HANDLE_VALUE,
        GetIpcSecurityAttributes(),
        PAGE_READWRITE,
        0,
        static_cast<DWORD>(sizeof(RMSharedTelemetry)),
        kIpcMapName);
    if (!map)
    {
        return nullptr;
    }

    bool created = (GetLastError() != ERROR_ALREADY_EXISTS);
    void* view = MapViewOfFile(map, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(RMSharedTelemetry));
    if (!view)
    {
        CloseHandle(map);
        return nullptr;
    }

    s_map = map;
    s_view = static_cast<RMSharedTelemetry*>(view);

    if (created)
    {
        ZeroMemory(s_view, sizeof(RMSharedTelemetry));
        s_view->version = kIpcVersion;
        s_view->size = sizeof(RMSharedTelemetry);
    }

    return s_view;
}

LONG AtomicRead(volatile LONG* value)
{
    return InterlockedCompareExchange(value, 0, 0);
}

} // namespace

extern "C" int rm_ipc_publish(double temperatureC, double powerW, double usagePercent, int status)
{
    RMSharedTelemetry* shared = GetSharedTelemetry();
    if (!shared)
    {
        return IPC_ERROR;
    }

    InterlockedIncrement(&shared->seq);
    shared->version = kIpcVersion;
    shared->size = sizeof(RMSharedTelemetry);
    shared->status = static_cast<uint32_t>(status);
    shared->timestamp_ms = GetTickCount64();
    shared->temperature_c = temperatureC;
    shared->power_w = powerW;
    shared->usage_percent = usagePercent;
    shared->writer_pid = GetCurrentProcessId();
    InterlockedIncrement(&shared->seq);

    return IPC_OK;
}

extern "C" int rm_ipc_read(
    double* temperatureC,
    double* powerW,
    double* usagePercent,
    int* status,
    unsigned int max_age_ms)
{
    if (!temperatureC || !powerW || !usagePercent)
    {
        return IPC_ERROR;
    }

    RMSharedTelemetry* shared = GetSharedTelemetry();
    if (!shared)
    {
        return IPC_NOT_READY;
    }

    if (shared->version != kIpcVersion || shared->size != sizeof(RMSharedTelemetry))
    {
        return IPC_NOT_READY;
    }

    RMSharedTelemetry snapshot{};
    for (int attempt = 0; attempt < 3; ++attempt)
    {
        LONG seq1 = AtomicRead(&shared->seq);
        if (seq1 & 1)
        {
            continue;
        }

        snapshot = *shared;
        MemoryBarrier();
        LONG seq2 = AtomicRead(&shared->seq);
        if (seq1 == seq2 && !(seq2 & 1))
        {
            break;
        }
        if (attempt == 2)
        {
            return IPC_NOT_READY;
        }
    }

    if (snapshot.timestamp_ms == 0)
    {
        return IPC_NOT_READY;
    }

    if (max_age_ms > 0)
    {
        ULONGLONG now = GetTickCount64();
        if (now >= snapshot.timestamp_ms &&
            now - snapshot.timestamp_ms > static_cast<ULONGLONG>(max_age_ms))
        {
            return IPC_STALE;
        }
    }

    *temperatureC = snapshot.temperature_c;
    *powerW = snapshot.power_w;
    *usagePercent = snapshot.usage_percent;
    if (status)
    {
        *status = static_cast<int>(snapshot.status);
    }

    return IPC_OK;
}

extern "C" int rm_ipc_service_start()
{
    if (g_ipc_service_event)
    {
        return 1;
    }

    g_ipc_service_event = CreateEventW(
        GetIpcSecurityAttributes(),
        TRUE,
        TRUE,
        kIpcServiceEventName);
    if (!g_ipc_service_event)
    {
        return 0;
    }
    SetEvent(g_ipc_service_event);
    return 1;
}

extern "C" void rm_ipc_service_stop()
{
    if (g_ipc_service_event)
    {
        CloseHandle(g_ipc_service_event);
        g_ipc_service_event = nullptr;
    }
}

extern "C" int rm_ipc_is_service_running()
{
    HANDLE event = OpenEventW(SYNCHRONIZE, FALSE, kIpcServiceEventName);
    if (!event)
    {
        return 0;
    }
    CloseHandle(event);
    return 1;
}

extern "C" int rm_ipc_owner_try_acquire()
{
    if (g_ipc_owner_held)
    {
        return 1;
    }

    HANDLE mutex = CreateMutexW(GetIpcSecurityAttributes(), FALSE, kIpcOwnerMutexName);
    if (!mutex)
    {
        return 0;
    }

    DWORD wait = WaitForSingleObject(mutex, 0);
    if (wait == WAIT_OBJECT_0 || wait == WAIT_ABANDONED)
    {
        g_ipc_owner_mutex = mutex;
        g_ipc_owner_held = true;
        return 1;
    }

    CloseHandle(mutex);
    return 0;
}

extern "C" void rm_ipc_owner_release()
{
    if (g_ipc_owner_mutex)
    {
        if (g_ipc_owner_held)
        {
            ReleaseMutex(g_ipc_owner_mutex);
        }
        CloseHandle(g_ipc_owner_mutex);
        g_ipc_owner_mutex = nullptr;
        g_ipc_owner_held = false;
    }
}
