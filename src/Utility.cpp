//===----------------------------------------------------------------------===
//=== Copyright (c) 2023-2025 Advanced Micro Devices, Inc.  All rights reserved.
//
//            Developed by: Advanced Micro Devices, Inc.

#include "Utility.hpp"
#include <intrin.h>
#include <windows.h>

static std::wstring g_monitor_sdk_path;

void SetMonitorSdkPath(const wchar_t* path)
{
	if (path && *path)
	{
		g_monitor_sdk_path = path;
	}
	else
	{
		g_monitor_sdk_path.clear();
	}
}

const wchar_t* GetMonitorSdkPath()
{
	if (g_monitor_sdk_path.empty())
	{
		return nullptr;
	}
	return g_monitor_sdk_path.c_str();
}

/** @brief  To check whether Vendor is AMD or not
*/
BOOL Authentic_AMD()
{
	char CPUString[0x20];
	int CPUInfo[4] = { -1 };
	char string[] = "AuthenticAMD";

	__cpuid(CPUInfo, 0);
	memset(CPUString, 0, sizeof(CPUString));
	*((int*)CPUString) = CPUInfo[1];
	*((int*)(CPUString + 4)) = CPUInfo[3];
	*((int*)(CPUString + 8)) = CPUInfo[2];

	if (!strcmp(string, CPUString))
		return true;
	else
		return false;

}


/*
*	@brief	Check if driver is installed or not
*/
INT QueryDrvService()
{
	SERVICE_STATUS ServiceStatus;
	SC_HANDLE hSCM = OpenSCManager(NULL, NULL, SC_MANAGER_ALL_ACCESS);
	if (!hSCM)
		return -1;

	SC_HANDLE hOpenService = OpenService(hSCM, RM_DRIVER_NAME, SC_MANAGER_ALL_ACCESS);
	if (GetLastError() == ERROR_SERVICE_DOES_NOT_EXIST)
	{
		CloseServiceHandle(hOpenService);
		CloseServiceHandle(hSCM);
		return   -1;
	}
	QueryServiceStatus(hOpenService, &ServiceStatus);
	if (ServiceStatus.dwCurrentState != SERVICE_RUNNING)
	{
		CloseServiceHandle(hOpenService);
		CloseServiceHandle(hSCM);
		return -1;
	}

	CloseServiceHandle(hOpenService);
	CloseServiceHandle(hSCM);
	return 0;
}

bool IsSupportedOS()
{
	bool bIsSupported = false;
	DWORD major = 0;
	DWORD minor = 0;
	LPBYTE pinfoRawData;
	if (IsWindowsServer())
	{
		return false;
	}
	if (NERR_Success == NetWkstaGetInfo(NULL, 100, &pinfoRawData))
	{
		WKSTA_INFO_100* pworkstationInfo = (WKSTA_INFO_100*)pinfoRawData;
		major = pworkstationInfo->wki100_ver_major;
		minor = pworkstationInfo->wki100_ver_minor;
		::NetApiBufferFree(pinfoRawData);
	}

	if (major >= 10)
	{
		bIsSupported = true;
	}
	
	return bIsSupported;
}

bool GetDriverPath(wchar_t* pDriverPath)
{
	const wchar_t* pTemp = GetMonitorSdkPath();
	wchar_t driverPath[200] = { '\0' };
	size_t iDriverPathLength = 0;

	LOG_PROCESS_ERROR(pTemp && *pTemp);

	iDriverPathLength = wcslen(pTemp);
	wcsncpy(driverPath, pTemp, iDriverPathLength);
	driverPath[iDriverPathLength] = '\0';

	wsprintf(pDriverPath, L"%s%s", driverPath, DRIVER_FILE_PATH_64);
	return true;
Exit0:
	return false;
}

bool InstallDriver(void)
{
	bool bRetCode = false;
	bool bResult = false;
	DWORD dwLastError;
	SC_HANDLE hSCManager = NULL;
	SC_HANDLE hService = NULL;
	wchar_t szDriverPath[256];


	HANDLE m_hDriver = CreateFile(L"\\\\.\\" RM_DRIVER_NAME,
		GENERIC_READ | GENERIC_WRITE,
		FILE_SHARE_READ | FILE_SHARE_WRITE,
		NULL,
		OPEN_EXISTING,
		FILE_ATTRIBUTE_NORMAL,
		NULL);

	if (m_hDriver == INVALID_HANDLE_VALUE)
	{
		bRetCode = GetDriverPath(szDriverPath);
		LOG_PROCESS_ERROR(bRetCode);

		hSCManager = OpenSCManager(NULL, NULL, SC_MANAGER_ALL_ACCESS);
		LOG_PROCESS_ERROR(hSCManager);

		// Install the driver
		hService = CreateService(hSCManager,
			RM_DRIVER_NAME, RM_DRIVER_NAME, SERVICE_ALL_ACCESS, SERVICE_KERNEL_DRIVER,
			SERVICE_AUTO_START, SERVICE_ERROR_NORMAL, szDriverPath,
			NULL, NULL, NULL, NULL, NULL);
		if (hService == NULL)
		{
			dwLastError = GetLastError();
			if (dwLastError == ERROR_SERVICE_EXISTS)
				hService = OpenService(hSCManager, RM_DRIVER_NAME, SERVICE_ALL_ACCESS);
			else if (dwLastError == ERROR_SERVICE_MARKED_FOR_DELETE)
			{
				hService = OpenService(hSCManager, RM_DRIVER_NAME, SERVICE_ALL_ACCESS);
				SERVICE_STATUS ServiceStatus;
				ControlService(hService, SERVICE_CONTROL_STOP, &ServiceStatus);
				CloseServiceHandle(hService);
				
				hService = CreateService(hSCManager,
					RM_DRIVER_NAME, RM_DRIVER_NAME, SERVICE_ALL_ACCESS, SERVICE_KERNEL_DRIVER,
					SERVICE_DEMAND_START, SERVICE_ERROR_NORMAL, szDriverPath,
					NULL, NULL, NULL, NULL, NULL);
			}
			else
				(void)GetLastError();
		}
		LOG_PROCESS_ERROR(hService);

		// Start the driver
		BOOL bRet = StartService(hService, 0, NULL);
		if (!bRet)
		{
			dwLastError = GetLastError();
			if (dwLastError == ERROR_PATH_NOT_FOUND)
			{
				bRet = DeleteService(hService);
				LOG_PROCESS_ERROR(bRet);

				CloseServiceHandle(hService);

				hService = CreateService(hSCManager,
					RM_DRIVER_NAME, RM_DRIVER_NAME, SERVICE_ALL_ACCESS, SERVICE_KERNEL_DRIVER,
					SERVICE_AUTO_START, SERVICE_ERROR_NORMAL, szDriverPath,
					NULL, NULL, NULL, NULL, NULL);
				LOG_PROCESS_ERROR(hService);

				bRet = StartService(hService, 0, NULL);
				LOG_PROCESS_ERROR(bRet);
			}

			if (dwLastError != ERROR_SERVICE_ALREADY_RUNNING)
			{
				LOG_PROCESS_ERROR(bRet);
			}
		}

		// Try to create the file again
		m_hDriver = CreateFile(L"\\\\.\\" RM_DRIVER_NAME,
			GENERIC_READ | GENERIC_WRITE,
			FILE_SHARE_READ | FILE_SHARE_WRITE,
			NULL,
			OPEN_EXISTING,
			FILE_ATTRIBUTE_NORMAL,
			NULL);
		LOG_PROCESS_ERROR(m_hDriver != INVALID_HANDLE_VALUE);
	}


	bResult = true;

Exit0:
	if (m_hDriver != INVALID_HANDLE_VALUE)
		CloseHandle(m_hDriver);

	if (hSCManager)
		CloseServiceHandle(hSCManager);
	if (hService)
		CloseServiceHandle(hService);


	return bResult;
}

bool g_GetRegistryValue(HKEY hRootKey, LPCWSTR keyPath, const wchar_t* valueName, std::wstring& ulValue, bool bIsDWORD)
{
	if (!valueName || (wcslen(valueName) == 0)) return false;
	HKEY hKey = NULL;
	DWORD dwLength = MAX_STRING_LEN;

	HRESULT hr = RegOpenKey(hRootKey, keyPath, &hKey);
	if (hr != ERROR_SUCCESS) return false;
	wchar_t buff[MAX_STRING_LEN] = { '\n' };

	if (bIsDWORD)
	{
		dwLength = sizeof(DWORD);
		DWORD dwValue = 0;
		hr = RegQueryValueEx(hKey, valueName, 0, NULL, reinterpret_cast<LPBYTE>(&dwValue), &dwLength);
		ulValue = std::to_wstring(dwValue);
	}
	else
	{
		hr = RegQueryValueEx(hKey, valueName, 0, NULL, reinterpret_cast<LPBYTE>(&buff), &dwLength);
		ulValue = std::wstring(buff);
	}
	RegCloseKey(hKey);

	return hr == ERROR_SUCCESS;
}
