//===----------------------------------------------------------------------===
//=== Copyright (c) 2023-2025 Advanced Micro Devices, Inc.  All rights reserved.
//
//            Developed by: Advanced Micro Devices, Inc.

#pragma once
#include <iostream>
#include <windows.h>
#include <tchar.h>
#include <Lm.h>
#include <VersionHelpers.h>
#include <intrin.h>
#include <string>
#include "GlobalDef.h"

#ifdef _MT
#include <process.h>
#endif
#define DRIVER_FILE_PATH_64	L"bin\\AMDRyzenMasterDriver.sys"
#define AMDRM_Monitoring_SDK_REGISTRY_PATH	L"Software\\AMD\\RyzenMasterMonitoringSDK"
#define MS_OS_Version_REGISTRY_PATH	L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion"

VOID ShowError(LPCTSTR userMsg, BOOL printErrorMsg, DWORD exitCODE);
BOOL Authentic_AMD();
INT QueryDrvService();
bool IsSupportedOS();
bool InstallDriver(void);
bool g_GetRegistryValue(HKEY hRootKey, LPCWSTR keyPath, const wchar_t* valueName, std::wstring& ulValue, bool bIsDWORD = false);
std::wstring GetSystemName();
std::wstring GetOSVersion();

#define LOG_PRINT(dRet, ch) (dRet==-1)?_tprintf(_T(" NA\n")) : _tprintf(_T(" %0.1f %s\n"), dRet, ch);

#define LOG_PROCESS_ERROR(__CONDITION__)		\
	do											\
	{											\
		if (!(__CONDITION__))					\
		{										\
			goto Exit0;							\
		}										\
	} while (false)