//===----------------------------------------------------------------------===
//=== Copyright (c) 2023-2025 Advanced Micro Devices, Inc.  All rights reserved.
//
//            Developed by: Advanced Micro Devices, Inc.

#pragma once
#include <windows.h>
#include <Lm.h>
#include <VersionHelpers.h>
#include <string>
#include "GlobalDef.h"

#ifdef _MT
#include <process.h>
#endif
#define DRIVER_FILE_PATH_64	L"bin\\AMDRyzenMasterDriver.sys"
#define AMDRM_Monitoring_SDK_REGISTRY_PATH	L"Software\\AMD\\RyzenMasterMonitoringSDK"
#define MS_OS_Version_REGISTRY_PATH	L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion"

BOOL Authentic_AMD();
INT QueryDrvService();
bool IsSupportedOS();
bool InstallDriver(void);
void SetMonitorSdkPath(const wchar_t* path);
const wchar_t* GetMonitorSdkPath();
bool g_GetRegistryValue(HKEY hRootKey, LPCWSTR keyPath, const wchar_t* valueName, std::wstring& ulValue, bool bIsDWORD = false);

#define LOG_PROCESS_ERROR(__CONDITION__)		\
	do											\
	{											\
		if (!(__CONDITION__))					\
		{										\
			goto Exit0;							\
		}										\
	} while (false)
