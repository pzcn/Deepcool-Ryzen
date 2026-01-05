//===----------------------------------------------------------------------===
//=== Copyright (c) 2023-2025 Advanced Micro Devices, Inc.  All rights reserved.
//
//            Developed by: Advanced Micro Devices, Inc.

#pragma once
#include <windows.h>
#include <Shlobj.h>
#include <intrin.h>
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
	bool bRetCode = false;
	std::wstring buff = {};
	DWORD dwTemp = 0;
	bRetCode = g_GetRegistryValue(HKEY_LOCAL_MACHINE, AMDRM_Monitoring_SDK_REGISTRY_PATH, L"InstallationPath", buff, dwTemp);
	if (!bRetCode)
	{
		return false;
	}

	std::wstring temp = buff + L"bin\\Platform.dll";
	ctx.hPlatform = LoadLibraryEx(temp.c_str(), NULL, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR);
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
	if (stData.stFreqData.dState && stData.stFreqData.uLength && stData.stFreqData.dCurrentFreq)
	{
		for (unsigned int i = 0; i < stData.stFreqData.uLength; ++i)
		{
			if (stData.stFreqData.dCurrentFreq[i] != 0)
			{
				occupancy_sum += stData.stFreqData.dState[i];
				occupancy_count++;
			}
		}
	}

	usagePercent = occupancy_count ? (occupancy_sum / occupancy_count) : 0.0;
	temperatureC = stData.dTemperature;
	powerW = stData.fPPTValue;
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
