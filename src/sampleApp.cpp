//===----------------------------------------------------------------------===
//=== Copyright (c) 2023-2025 Advanced Micro Devices, Inc.  All rights reserved.
//
//            Developed by: Advanced Micro Devices, Inc.

#pragma once
#include <windows.h>
#include <Shlobj.h>
#include <iostream>
#include <VersionHelpers.h>
#include <intrin.h>
#include <fstream>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <thread>
#include <filesystem>
#include <powrprof.h>
#include <guiddef.h>
#include "ICPUEx.h"
#include "IPlatform.h"
#include "IDeviceManager.h"
#include "IBIOSEx.h"
#include <regex>

#include "Utility.hpp"
#pragma comment(lib, "PowrProf.lib")

#define MAX_LENGTH 50
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

TCHAR PrintErr[][63] = { _T("Failure"),_T("Success") ,_T("Invalid value"),_T("Method is not implemented by the BIOS"),_T("Cores are already parked. First Enable all the cores"), _T("Unsupported Function") };


inline void printFunc(LPCTSTR func, BOOL bCore, int i = 0)
{
	if (!bCore)
	{
		_tprintf(_T("%s "), func);
		for (size_t j = 0; j < MAX_LENGTH - _tcslen(func); ++j)
		{
			_tprintf(_T("%c"), '.');
		}
	}
	else
	{
		_tprintf(_T("%s Core : %-2d"), func, i);
		for (size_t j = 0; j < MAX_LENGTH - _tcslen(func) - 9; ++j)
		{
			_tprintf(_T("%c"), '.');
		}
	}
}

std::pair<std::wstring, std::wstring> GetCurrentTimeString(bool bFileName = false)
{
	auto now = std::chrono::system_clock::now();
	std::time_t now_time = std::chrono::system_clock::to_time_t(now);
	std::tm now_tm;
	localtime_s(&now_tm, &now_time);

	std::wostringstream dateStream;
	std::wostringstream timeStream;
	dateStream << std::put_time(&now_tm, L"%Y-%m-%d");

	if (bFileName)
		timeStream << std::put_time(&now_tm, L"%H-%M-%S");
	else
	{
		auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
		auto us = std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch()) % 1000;

		timeStream << std::put_time(&now_tm, L"%H:%M:%S")
			<< L'.' << std::setfill(L'0') << std::setw(3) << ms.count()
			<< std::setfill(L'0') << std::setw(3) << us.count();
	}
	return { dateStream.str(), timeStream.str() };
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

void apiCall()
{
bool bRetCode = false;
	HMODULE hPlatform_Handle = nullptr;
	LPCWSTR path = {};
	std::wstring buff = {};
	DWORD dwTemp = 0;
	bRetCode = g_GetRegistryValue(HKEY_LOCAL_MACHINE, AMDRM_Monitoring_SDK_REGISTRY_PATH, L"InstallationPath", buff, dwTemp);
	if (!bRetCode)
	{
		_tprintf(_T("Unexpected Error E1001. Please reinstall AMDRyzenMasterMonitoringSDK\n"));
		return;
	}

	std::wstring temp = buff + L"bin\\Platform.dll";
	path = temp.c_str();

	hPlatform_Handle = LoadLibraryEx(path, NULL, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR);
	if (!hPlatform_Handle)
	{
		_tprintf(_T("Unexpected Error E1004. Please reinstall AMDRyzenMasterMonitoringSDK\n"));
		return;
	}

	GetPlatformFunc platformFunc = (GetPlatformFunc)GetProcAddress(hPlatform_Handle, "GetPlatform");
	if (!platformFunc)
	{
		FreeLibrary(hPlatform_Handle);
		_tprintf(_T("Platform init failed\n"));
		return;
	}
	IPlatform& rPlatform = platformFunc();
	bRetCode = rPlatform.Init();
	if (!bRetCode)
	{
		FreeLibrary(hPlatform_Handle);
		_tprintf(_T("Platform init failed\n"));
		return;
	}
	IDeviceManager& rDeviceManager = rPlatform.GetIDeviceManager();
	ICPUEx* obj = (ICPUEx*)rDeviceManager.GetDevice(dtCPU, 0);
	IBIOSEx* objB = (IBIOSEx*)rDeviceManager.GetDevice(dtBIOS, 0);
	if (obj && objB)
	{
		CACHE_INFO result;
		unsigned long uResult = 0;
		unsigned int uCorePark = 0;
		unsigned int uCoreCount = 0;
		wchar_t package[30] = { 0 };
		PWCHAR wResult = NULL;
		PTCHAR tResult = NULL;
		WCHAR sDate[50] = { '\0' };
		WCHAR Year[5] = { '\0' };
		WCHAR Month[3] = { '\0' };
		WCHAR Day[3] = { '\0' };
		WCHAR ChipsetID[256] = { '\0' };
		CPUParameters stData;
		int iRet = 0;
		int i, j = 0;
		unsigned short uVDDIO = 0, uMemClock = 0;
		unsigned char uTcl = 0, uTcdrd = 0, uTras = 0, uTrp = 0;

		wResult = (PWCHAR)obj->GetName();
		printFunc(_T("GetName"), FALSE);
		wprintf(L" %s\n", wResult);

		iRet = obj->GetFamily(uResult);
		printFunc(_T("GetFamily"), FALSE);
		if (iRet)
		{
			_tprintf(_T(" %s\n"), PrintErr[iRet + 1]);
		}
		else
		{
			_tprintf(_T(" %lu\n"), uResult);
		}

		iRet = obj->GetModel(uResult);
		printFunc(_T("GetModel"), FALSE);
		if (iRet)
		{
			_tprintf(_T(" %s\n"), PrintErr[iRet + 1]);
		}
		else
		{
			_tprintf(_T(" %lu\n"), uResult);
		}

		iRet = obj->GetStepping(uResult);
		printFunc(_T("GetStepping"), FALSE);
		if (iRet)
		{
			_tprintf(_T(" %s\n"), PrintErr[iRet + 1]);
		}
		else
		{
			_tprintf(_T(" %lu\n"), uResult);
		}

		wResult = (PWCHAR)obj->GetDescription();
		printFunc(_T("GetDescription"), FALSE);
		wprintf(L" %s\n", wResult);
		wResult = (PWCHAR)obj->GetVendor();
		printFunc(_T("GetVendor"), FALSE);
		wprintf(L" %s\n", wResult);
		tResult = (PTCHAR)obj->GetClassName();
		printFunc(_T("GetClassName"), FALSE);
		_tprintf(_T(" %s\n"), tResult);

		iRet = obj->GetCoreCount(uCoreCount);
		printFunc(_T("GetCoreCount"), FALSE);
		if (iRet)
		{
			_tprintf(_T(" %s\n"), PrintErr[iRet + 1]);
		}
		else
		{
			_tprintf(_T(" %u\n"), uCoreCount);
		}

		iRet = obj->GetCorePark(uCorePark);
		printFunc(_T("GetCorePark"), FALSE);
		if (iRet)
		{
			_tprintf(_T(" %s\n"), PrintErr[iRet + 1]);
		}
		else
		{
			_tprintf(_T(" %lu Cores parked\n"), uCorePark);
		}

		iRet = obj->GetL1DataCache(result);
		printFunc(_T("GetL1DataCache"), FALSE);
		if (iRet)
		{
			_tprintf(_T(" %s\n"), PrintErr[iRet + 1]);
		}
		else
		{
			_tprintf(_T(" %d x %.0fKB\n"), (uCoreCount - uCorePark), result.fSize);
		}

		result = { NULL };
		iRet = obj->GetL1InstructionCache(result);
		printFunc(_T("GetL1InstructionCache"), FALSE);
		if (iRet)
		{
			_tprintf(_T(" %s\n"), PrintErr[iRet + 1]);
		}
		else
		{
			_tprintf(_T(" %d x %.0fKB\n"), (uCoreCount - uCorePark), result.fSize);
		}

		result = { NULL };
		iRet = obj->GetL2Cache(result);
		printFunc(_T("GetL2Cache"), FALSE);
		if (iRet)
		{
			_tprintf(_T(" %s\n"), PrintErr[iRet + 1]);
		}
		else
		{
			_tprintf(_T(" %d x %.0fKB\n"), (uCoreCount - uCorePark), result.fSize);
		}

		result = { NULL };
		iRet = obj->GetL3Cache(result);
		printFunc(_T("GetL3Cache"), FALSE);
		if (iRet)
		{
			_tprintf(_T(" %s\n"), PrintErr[iRet + 1]);
		}
		else
		{
			_tprintf(_T(" %0.f KB\n"), result.fSize);
		}

		printFunc(_T("GetPackage"), FALSE);
		wprintf(L" %s\n", obj->GetPackage());

		iRet = obj->GetCPUParameters(stData);
		printFunc(_T("GetCPUParameters"), FALSE);
		if (iRet)
		{
			_tprintf(_T(" %s\n"), PrintErr[iRet + 1]);
		}
		else
		{
			_tprintf(_T(" PPT Current Limit :"));
			LOG_PRINT(stData.fPPTLimit, _T("W"));

			printFunc(_T("GetCPUParameters"), FALSE);
			_tprintf(_T(" PPT Current Value :"));
			LOG_PRINT(stData.fPPTValue, _T("W"));

			printFunc(_T("GetCPUParameters"), FALSE);
			_tprintf(_T(" EDC(VDD) Current Limit :"));
			LOG_PRINT(stData.fEDCLimit_VDD, _T("A"));

			printFunc(_T("GetCPUParameters"), FALSE);
			_tprintf(_T(" EDC(VDD) Current Value :"));
			LOG_PRINT(stData.fEDCValue_VDD, _T("A"));


			printFunc(_T("GetCPUParameters"), FALSE);
			_tprintf(_T(" EDC(VDD)_1 Current Value :"));
			LOG_PRINT(stData.fEDCValue_VDD_1, _T("A"));
			

			printFunc(_T("GetCPUParameters"), FALSE);
			_tprintf(_T(" TDC(VDD) Current Limit :"));
			LOG_PRINT(stData.fTDCLimit_VDD, _T("A"));

			printFunc(_T("GetCPUParameters"), FALSE);
			_tprintf(_T(" TDC(VDD) Current Value :"));
			LOG_PRINT(stData.fTDCValue_VDD, _T("A"));

			printFunc(_T("GetCPUParameters"), FALSE);
			_tprintf(_T(" TDC(VDD)_1 Current Value :"));
			LOG_PRINT(stData.fTDCValue_VDD_1, _T("A"));

			printFunc(_T("GetCPUParameters"), FALSE);
			_tprintf(_T(" EDC(SOC) Current Limit :"));
			LOG_PRINT(stData.fEDCLimit_SOC, _T("A"));

			printFunc(_T("GetCPUParameters"), FALSE);
			_tprintf(_T(" EDC(SOC) Current Value :"));
			LOG_PRINT(stData.fEDCValue_SOC, _T("A"));

			printFunc(_T("GetCPUParameters"), FALSE);
			_tprintf(_T(" TDC(SOC) Current Limit :"));
			LOG_PRINT(stData.fTDCLimit_SOC, _T("A"));

			printFunc(_T("GetCPUParameters"), FALSE);
			_tprintf(_T(" TDC(SOC) Current Value :"));
			LOG_PRINT(stData.fTDCValue_SOC, _T("A"));

			printFunc(_T("GetCPUParameters"), FALSE);
			_tprintf(_T(" EDC(CCD) Current Limit :"));
			LOG_PRINT(stData.fEDCLimit_CCD, _T("A"));

			printFunc(_T("GetCPUParameters"), FALSE);
			_tprintf(_T(" EDC(CCD) Current Value :"));
			LOG_PRINT(stData.fEDCValue_CCD, _T("A"));

			printFunc(_T("GetCPUParameters"), FALSE);
			_tprintf(_T(" TDC(CCD) Current Limit :"));
			LOG_PRINT(stData.fTDCLimit_CCD, _T("A"));

			printFunc(_T("GetCPUParameters"), FALSE);
			_tprintf(_T(" TDC(CCD) Current Value :"));
			LOG_PRINT(stData.fTDCValue_CCD, _T("A"));

			printFunc(_T("GetCPUParameters"), FALSE);
			_tprintf(_T(" cHTC Limit :"));
			LOG_PRINT(stData.fcHTCLimit, _T("Celsius"));

			printFunc(_T("GetCPUParameters"), FALSE);
			_tprintf(_T(" Fabric Clock Frequency :"));
			LOG_PRINT(stData.fFCLKP0Freq, _T("MHz"));

			printFunc(_T("GetCPUParameters"), FALSE);
			_tprintf(_T(" VDDCR(VDD) Power :"));
			LOG_PRINT(stData.fVDDCR_VDD_Power, _T("W"));

			printFunc(_T("GetCPUParameters"), FALSE);
			_tprintf(_T(" VDDCR(SOC) Power :"));
			LOG_PRINT(stData.fVDDCR_SOC_Power, _T("W"));

			printFunc(_T("GetCPUParameters"), FALSE);
			_tprintf(_T(" Fmax(CPU Clock) frequency :"));
			LOG_PRINT(stData.fCCLK_Fmax, _T("MHz"));

			printFunc(_T("GetCurrentOCMode"), FALSE);
			if (stData.eMode.uManual)
			{
				_tprintf(_T(" Manual Mode.\n"));
			}
			else if (stData.eMode.uPBOMode)
			{
				_tprintf(_T(" PBO Mode.\n"));
			}
			else if (stData.eMode.uAutoOverclocking)
			{
				_tprintf(_T(" Auto Overclocking Mode.\n"));
			}
			else if (stData.eMode.uEcoMode)
			{
				_tprintf(_T(" ECO Mode.\n"));
			}
			else if (stData.eMode.uDefault_IRM)
			{
				_tprintf(_T(" Default_IRM Mode.\n"));
			}
			else
			{
				_tprintf(_T(" Default Mode.\n"));
			}

			_tprintf(_T("Core Frequency ....................................\n"));
			std::cout << std::left << std::setw(10) << "\tCore" << std::setw(15) << "Freq(MHz)" << std::setw(16) << "Freq Eff(MHz)" << std::setw(15) << "C0 Residency" << std::setw(15) << "Temperature(C)" << std::endl;
			for (i = 0; i < stData.stFreqData.uLength && nullptr != stData.stFreqData.dCurrentFreq; i++)
			{
				if (stData.stFreqData.dCurrentFreq[i] != 0)
				{

					std::cout << std::fixed << std::setprecision(1) << std::left << "\t" << std::setw(10) << j
						<< std::setw(15) << stData.stFreqData.dCurrentFreq[i] << std::setw(15) << stData.stFreqData.dFreq[i] << std::setw(15)
						<< stData.stFreqData.dState[i] << std::setw(15) << stData.stFreqData.dCurrentTemp[i] << std::endl;
					j++;
				}
			}

			printFunc(_T("GetPeakSpeed"), FALSE);
			_tprintf(_T(" %0.1f MHz\n"), stData.dPeakSpeed);
			printFunc(_T("GetPeakCore(s)Voltage"), FALSE);
			_tprintf(_T(" %0.3f V\n"), stData.dPeakCoreVoltage);
			
			printFunc(_T("GetPeakCore(s)Voltage_1"), FALSE);
			if (stData.dPeakCoreVoltage_1 != -1)
			{
				_tprintf(_T(" %0.3f V\n"), stData.dPeakCoreVoltage_1);
			}
			else
			{
				_tprintf(_T(" NA\n"));
			}
			printFunc(_T("GetAverageCoreVoltage"), FALSE);
			_tprintf(_T(" %0.3f V\n"), stData.dAvgCoreVoltage);

			printFunc(_T("GetAverageCoreVoltage_1"), FALSE);
			if (stData.dAvgCoreVoltage_1 != -1)
			{
				_tprintf(_T(" %0.3f V\n"), stData.dAvgCoreVoltage_1);
			}
			else
			{
				_tprintf(_T(" NA\n"));
			}
			printFunc(_T("GetVDDCR_SOC"), FALSE);
			_tprintf(_T(" %0.3f V\n"), stData.dSocVoltage);;
			printFunc(_T("GetCurrentTemperature"), FALSE);
			_tprintf(_T(" %0.2f Celsius\n"), stData.dTemperature);
		}

		iRet = objB->GetMemVDDIO(uVDDIO);
		printFunc(_T("GetMemVDDIO"), FALSE);
		if (iRet)
		{
			_tprintf(_T(" %s\n"), PrintErr[iRet + 1]);
		}
		else
		{
			_tprintf(_T(" %d mV\n"), uVDDIO);
		}

		iRet = objB->GetCurrentMemClock(uMemClock);
		printFunc(_T("GetCurrentMemClock"), FALSE);
		if (iRet)
		{
			_tprintf(_T(" %s\n"), PrintErr[iRet + 1]);
		}
		else
		{
			_tprintf(_T(" %d MHz\n"), uMemClock);
		}

		iRet = objB->GetMemCtrlTcl(uTcl);
		printFunc(_T("GetMemCtrlTcl"), FALSE);
		if (iRet)
		{
			_tprintf(_T(" %s\n"), PrintErr[iRet + 1]);
		}
		else
		{
			_tprintf(_T(" %d Memory clock cycles\n"), uTcl);
		}

		iRet = objB->GetMemCtrlTrcdrd(uTcdrd);
		printFunc(_T("GetMemCtrlTrcdrd"), FALSE);
		if (iRet)
		{
			_tprintf(_T(" %s\n"), PrintErr[iRet + 1]);
		}
		else
		{
			_tprintf(_T(" %d Memory clock cycles\n"), uTcdrd);
		}

		iRet = objB->GetMemCtrlTras(uTras);
		printFunc(_T("GetMemCtrlTras"), FALSE);
		if (iRet)
		{
			_tprintf(_T(" %s\n"), PrintErr[iRet + 1]);
		}
		else
		{
			_tprintf(_T(" %d Memory clock cycles\n"), uTras);
		}

		iRet = objB->GetMemCtrlTrp(uTrp);
		printFunc(_T("GetMemCtrlTrp"), FALSE);
		if (iRet)
		{
			_tprintf(_T(" %s\n"), PrintErr[iRet + 1]);
		}
		else
		{
			_tprintf(_T(" %d Memory clock cycles\n"), uTrp);
		}

		printFunc(_T("GetBIOSInfo"), FALSE);
		wcscpy(sDate, objB->GetDate());
		wcsncpy(Year, sDate, 4);
		Year[4] = '\0';
		wcsncpy(Month, sDate + 4, 2);
		Month[2] = '\0';
		wcsncpy(Day, sDate + 6, 2);
		Day[2] = '\0';
		wprintf(L" Version : %s , Vendor : %s , Date : %s/%s/%s\n", objB->GetVersion(), objB->GetVendor(), Year, Month, Day);

		printFunc(_T("GetChipsetName"), FALSE);
		iRet = obj->GetChipsetName(ChipsetID);
		if (iRet)
		{
			_tprintf(_T(" %s\n"), PrintErr[iRet + 1]);
		}
		else
		{
			wprintf(L" %s\n", ChipsetID);
		}
	}
	rPlatform.UnInit();
	FreeLibrary(hPlatform_Handle);
}

std::wstring GetPowerSchemeName(const GUID* schemeGuid)
{
	DWORD bufferSize = 0;
	DWORD dwRetValue = PowerReadFriendlyName(NULL, schemeGuid, NULL, NULL, NULL, &bufferSize);

	if (dwRetValue != ERROR_SUCCESS || bufferSize == 0)
	{
		return L"Unknown";
	}

	std::wstring friendlyName(bufferSize / sizeof(WCHAR), L'\0');
	dwRetValue = PowerReadFriendlyName(NULL, schemeGuid, NULL, NULL, reinterpret_cast<BYTE*>(&friendlyName[0]), &bufferSize);

	if (dwRetValue != ERROR_SUCCESS)
	{
		return L"Unknown";
	}
	return friendlyName;
}

void LogCPUParameters(unsigned int iDuration, unsigned int iInterval, std::wstring wsFileString)
{
	bool bRetCode = false;
	HMODULE hPlatform_Handle = nullptr;
	std::wstring wsSDKPathRegValue = {};
	auto DateTimeForFileName = GetCurrentTimeString(true);

	std::wstring wsFileName = L"RMSDK_Parameter_log_" + (wsFileString.length() ? (wsFileString + L"_") : L"") + DateTimeForFileName.first + L"-" + DateTimeForFileName.second + L".csv";

	std::wofstream file(wsFileName, std::ios::app);
	if (file.is_open())
	{
		LPCWSTR path = {};
		DWORD dwTemp = 0;
		bRetCode = g_GetRegistryValue(HKEY_LOCAL_MACHINE, AMDRM_Monitoring_SDK_REGISTRY_PATH, L"InstallationPath", wsSDKPathRegValue, dwTemp);
		if (!bRetCode)
		{
			_tprintf(_T("Unexpected Error E1001. Please reinstall AMDRyzenMasterMonitoringSDK\n"));
			return;
		}

		std::wstring temp = wsSDKPathRegValue + L"bin\\Platform.dll";
		path = temp.c_str();

		hPlatform_Handle = LoadLibraryEx(path, NULL, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR);
		if (!hPlatform_Handle)
		{
			_tprintf(_T("Unexpected Error E1004. Please reinstall AMDRyzenMasterMonitoringSDK\n"));
			return;
		}

		GetPlatformFunc platformFunc = (GetPlatformFunc)GetProcAddress(hPlatform_Handle, "GetPlatform");
		if (!platformFunc)
		{
			FreeLibrary(hPlatform_Handle);
			_tprintf(_T("Platform init failed\n"));
			return;
		}
		IPlatform& rPlatform = platformFunc();
		bRetCode = rPlatform.Init();
		if (!bRetCode)
		{
			FreeLibrary(hPlatform_Handle);
			_tprintf(_T("Platform init failed\n"));
			return;
		}
		IDeviceManager& rDeviceManager = rPlatform.GetIDeviceManager();
		ICPUEx* obj = (ICPUEx*)rDeviceManager.GetDevice(dtCPU, 0);
		IBIOSEx* objB = (IBIOSEx*)rDeviceManager.GetDevice(dtBIOS, 0);

		if (obj && objB)
		{
			WCHAR wDate[50] = { '\0' };
			WCHAR wYear[5] = { '\0' };
			WCHAR wMonth[3] = { '\0' };
			WCHAR wDay[3] = { '\0' };
			auto fullPath = std::filesystem::absolute(wsFileName);
			HKEY UserRootPowerKey = NULL;
			GUID* activeSchemeGuid = nullptr;
			SYSTEM_POWER_STATUS sps;
			wcscpy_s(wDate, objB->GetDate());
			wcsncpy_s(wYear, wDate, 4);
			wYear[4] = '\0';
			wcsncpy_s(wMonth, wDate + 4, 2);
			wMonth[2] = '\0';
			wcsncpy_s(wDay, wDate + 6, 2);
			wDay[2] = '\0';
			CPUParameters stData;
			unsigned int uCorePark = 0;
			int iRet = 0;

			iRet = obj->GetCPUParameters(stData);
			if (iRet)
			{
				if (iRet == bUnsupportedFunction)
				{
					std::cerr << "Logging Feature is not supported. Exiting...\n";
					return;
				}
				std::cerr << "Failed to get the CPU Parameters. Error Code : ";
				_tprintf(_T(" %s\n"), PrintErr[iRet + 1]);
				return;
			}

			iRet = obj->GetCorePark(uCorePark);

			if (iRet)
			{
				std::cerr << "Failed to get the Core park information. Error Code : ";
				_tprintf(_T(" %s\n"), PrintErr[iRet + 1]);
				return;
			}

			file << L"System Details" << std::endl;
			file << L"System Name," << GetSystemName() << std::endl;
			file << L"Processor Name," << (obj->GetName()) << std::endl;
			file << L"OS Version," << GetOSVersion() << std::endl;
			file << L"BIOS date," << (wYear) << L"/" << (wMonth) << "/" << (wDay) << std::endl;
			file << L"BIOS Version," << (objB->GetVersion()) << std::endl;
			file << L"BIOS Vendor," << (objB->GetVendor()) << std::endl;
			file << std::endl << std::endl;
			
			auto logStart = std::chrono::steady_clock::now();
			auto logEnd = logStart + std::chrono::minutes(iDuration);

			file << "Date ,Time ,Active Power Scheme ,AC power status,";
			file << "PPT Current Limit (W) ,PPT Current Value (W),";
			file << "EDC(VDD) Current Limit (A) ,EDC(VDD) Current Value (A),";
			if (stData.fEDCValue_VDD_1 != -1)
			{
				file << "EDC(VDD)_1 Current Value (A),";
			}
			file << "TDC(VDD) Current Limit (A) ,TDC(VDD) Current Value (A),";
			if (stData.fTDCValue_VDD_1 != -1)
			{
				file << "TDC(VDD)_1 Current Value (A),";
			}
			if (stData.fEDCLimit_SOC != -1)
			{
				file << "EDC(SOC) Current Limit (A),";
			}
			if (stData.fEDCValue_SOC != -1)
				file << "EDC(SOC) Current Value (A) ,";
			if (stData.fTDCLimit_SOC != -1)
				file << "TDC(SOC) Current Limit (A) ,";
			if (stData.fTDCValue_SOC != -1)
				file << "TDC(SOC) Current Value (A) ,";
			if (stData.fEDCLimit_CCD != -1)
				file << "EDC(CCD) Current Limit (A),";
			if (stData.fEDCValue_CCD != -1)
				file << "EDC(CCD) Current Value (A),";
			if (stData.fTDCLimit_CCD != -1)
				file << "TDC(CCD) Current Limit (A),";
			if (stData.fTDCValue_CCD != -1)
				file << "TDC(CCD) Current Value (A),";

			file << "Fabric Clock Frequency	(MHz),";
			file << "VDDCR(VDD) Power (W),";
			file << "VDDCR(SOC) Power (W),";
			file << "Fmax(CPU Clock) frequency (MHz),";
			file << "Peak Speed (MHz),";
			file << "PeakCore(s) Voltage (V),";
			file << "Average Core Voltage (V),";
			file << "VDDCR_SOC (V),";
			file << "cHTC Limit (Celsius),";
			file << "Current Temperature (Celsius),";

			if (stData.dPeakCoreVoltage_1 != -1)
				file << "PeakCore(s) Voltage_1 (V),";
			if (stData.dAvgCoreVoltage_1 != -1)
				file << "Average Core Voltage_1 (V),";

			file << "Current OCMode,";

			for (int i = 0; i < stData.stFreqData.uLength - uCorePark; i++)
			{
				file << "Core " << i << " EffectiveFrequency (MHz),";
			}
			for (int i = 0; i < stData.stFreqData.uLength - uCorePark; i++)
			{
				file << "Core " << i << " C0 Residency (%),";
			}
			for (int i = 0; i < stData.stFreqData.uLength - uCorePark; i++)
			{
				file << "Core " << i << " CurrentFrequency (MHz),";
			}
			for (int i = 0; i < stData.stFreqData.uLength - uCorePark; i++)
			{
				file << "Core " << i << " CurrentTemperature (C),";
			}

			std::cout << "\nLogging with " << iInterval << " seconds sampling interval for " << iDuration << " minutes to file : \n\n" << fullPath << std::endl << std::endl;

			while (logStart < logEnd)
			{
				auto timeStampForLog = GetCurrentTimeString();
				
				iRet = obj->GetCPUParameters(stData);

				if (iRet)
				{
					_tprintf(_T(" %s\n"), PrintErr[iRet + 1]);
				}
				else
				{
					file << std::endl;
					file << timeStampForLog.first << "," << timeStampForLog.second << ",";
					DWORD result = PowerGetActiveScheme(UserRootPowerKey, &activeSchemeGuid);
					if (result == ERROR_SUCCESS)
					{
						std::wstring schemeName = GetPowerSchemeName(activeSchemeGuid);
						file << schemeName << ",";
						LocalFree(activeSchemeGuid);
					}
					else
					{
						file << L"Unknown Power Scheme,";
						std::cerr << "Failed to get the active power scheme. Error code: " << result << std::endl;
					}

					if (GetSystemPowerStatus(&sps))
					{
						std::wstring strACStatus = L"Unknown status,";
						if (sps.ACLineStatus == 0)
							strACStatus = L"DC,";
						else if (sps.ACLineStatus == 1)
							strACStatus = L"AC,";
						file << strACStatus;
					}
					else
					{
						file << L"Unknown Status,";
						std::cerr << "Failed to get the AC power status." << std::endl;
					}

					file << stData.fPPTLimit << "," << stData.fPPTValue << ",";
					file << stData.fEDCLimit_VDD << "," << stData.fEDCValue_VDD << ",";
					if (stData.fEDCValue_VDD_1 != -1)
						file << stData.fEDCValue_VDD_1 << ",";
					file << stData.fTDCLimit_VDD << "," << stData.fTDCValue_VDD << ",";
					if (stData.fTDCValue_VDD_1 != -1)
						file << stData.fTDCValue_VDD_1 << ",";
					if (stData.fEDCLimit_SOC != -1)
						file << stData.fEDCLimit_SOC << ",";
					if (stData.fEDCValue_SOC != -1)
						file << stData.fEDCValue_SOC << ",";
					if (stData.fTDCLimit_SOC != -1)
						file << stData.fTDCLimit_SOC << ",";
					if (stData.fTDCValue_SOC != -1)
						file << stData.fTDCValue_SOC << ",";
					if (stData.fEDCLimit_CCD != -1)
						file << stData.fEDCLimit_CCD << ",";
					if (stData.fEDCValue_CCD != -1)
						file << stData.fEDCValue_CCD << ",";
					if (stData.fTDCLimit_CCD != -1)
						file << stData.fTDCLimit_CCD << ",";
					if (stData.fTDCValue_CCD != -1)
						file << stData.fTDCValue_CCD << ",";

					file << stData.fFCLKP0Freq << ",";
					file << stData.fVDDCR_VDD_Power << ",";
					file << stData.fVDDCR_SOC_Power << ",";
					file << stData.fCCLK_Fmax << ",";
					file << stData.dPeakSpeed << ",";
					file << stData.dPeakCoreVoltage << ",";
					file << stData.dAvgCoreVoltage << ",";
					file << stData.dSocVoltage << ",";
					file << stData.fcHTCLimit << ",";
					file << stData.dTemperature << ",";

					if (stData.dPeakCoreVoltage_1 != -1)
					{
						file << stData.dPeakCoreVoltage_1 << ",";
					}
					if (stData.dAvgCoreVoltage_1 != -1)
					{
						file << stData.dAvgCoreVoltage_1 << ",";
					}

					if (stData.eMode.uManual)
					{
						file << "Manual Mode,";
					}
					else if (stData.eMode.uPBOMode)
					{
						file << "PBO Mode,";
					}
					else if (stData.eMode.uAutoOverclocking)
					{
						file << "Auto Overclocking Mode,";
					}
					else if (stData.eMode.uEcoMode)
					{
						file << "ECO Mode,";
					}
					else if (stData.eMode.uDefault_IRM)
					{
						file << "Default_IRM Mode,";
					}
					else
					{
						file << "Default Mode,";
					}

					for (int i = 0; i < stData.stFreqData.uLength - uCorePark; i++)
					{
						file << stData.stFreqData.dFreq[i] << ",";
					}
					for (int i = 0; i < stData.stFreqData.uLength - uCorePark; i++)
					{
						file << stData.stFreqData.dState[i] << ",";
					}
					for (int i = 0; i < stData.stFreqData.uLength - uCorePark; i++)
					{
						file << stData.stFreqData.dCurrentFreq[i] << ",";
					}
					for (int i = 0; i < stData.stFreqData.uLength - uCorePark; i++)
					{
						file << stData.stFreqData.dCurrentTemp[i] << ",";
					}
				}
				std::this_thread::sleep_for(std::chrono::seconds(iInterval));
				logStart = std::chrono::steady_clock::now();
			}
		}
		std::cout << "Logging Completed ... " << std::endl;
		file.close();
	}
	else
	{
		std::wcerr << L"Unable to open file for logging.\n file name : " << wsFileName << std::endl;
	}
}

/** @brief	Pattern matches with the string, ingnores cases
*	@return	TRUE : if strings matched
*			FALSE: if string not matched
*/
INT PatternMatch(LPCSTR s1, LPCSTR s2)
{
	int i;
	for (i = 0; s1[i] && s2[i]; ++i)
	{
		/* If characters are same or inverting the 6th bit makes them same */
		if (s1[i] == s2[i] || (s1[i] ^ 32) == s2[i])
			continue;
		else
			break;
	}

	/* Compare the last (or first mismatching in case of not same) characters */
	if (s1[i] == s2[i])
		return 0;
	if ((s1[i] | 32) < (s2[i] | 32))
		return -1;
	return 1;
}

void ShowUsage()
{
	std::cout << "Options: " << std::endl;
	std::cout << "\t-H     \t  : Show help\n\t-L   \t  : Log CPU Parameters" << std::endl;
	std::cout << "\t\tTo Generate logs use -L option as follows :" << std::endl;
	std::cout << "\t\t-L <Total log duration in minutes> <Log interval in seconds> <Optional String to be append in log file name>" << std::endl;
}
bool IsInteger(const char* str)
{
	if (str == nullptr || *str == '\0')
		return false;

	for (size_t i = 0; i < strlen(str); ++i)
	{
		if (!std::isdigit(str[i]))
			return false;
	}

	return true;
}

void Process_program_options(const int argc, char* const argv[])
{
	try
	{
		if (argc == 1)
		{
			apiCall();
			return;
		}
		if (argc != 4 && argc != 5)
		{
			ShowUsage();
			return;
		}
		if ((PatternMatch(argv[1], "-L") == 0) || (PatternMatch(argv[1], "--log") == 0))
		{
			if (IsInteger(argv[2]) && IsInteger(argv[3]))
			{
				unsigned int iDuration = atoi(argv[2]);
				unsigned int iInterval = atoi(argv[3]);
				std::wstring wsFileString = L"";
				if (argc == 5)
				{
					wsFileString = std::wstring(argv[4], argv[4] + strlen(argv[4]));
					std::wregex validFileNameRegex(LR"(^[^<>:"/\\|?*]+$)");
					bool status = std::regex_match(wsFileString, validFileNameRegex);
					if (!status)
					{
						throw (L"File name is not valid");
					}
				}
				if (iInterval > iDuration * 60)
					throw (L"Log interval can not be greater than total log duration");
				if (iInterval == 0 || iDuration == 0)
					throw (L"Log interval and Log Duration can not be 0");
				LogCPUParameters(iDuration, iInterval, wsFileString);
				return;
			}
			else
			{
				ShowUsage();
				return;
			}
		}
		ShowUsage();
	}
	catch (LPCTSTR strErr)
	{
		ShowError(strErr, FALSE, 1);
	}
	return;
}

int main(int argc, CHAR **argv)
{
	//Check if application running with admin privileged or not
	if (!IsUserAnAdmin())
	{
		MessageBox(NULL, _T("Access Denied : Run application with Admin rights"), TEXT("Error"), MB_OK);
		ShowError(_T("User is not admin..."), FALSE, 1);
	}
	//Check for OS support
	if (!IsSupportedOS())
	{
		ShowError(_T("This Desktop application requires OS version greater than or equals to Windows 10."), FALSE, 1);
	}
	//Check if we have AMD processor
	if (!Authentic_AMD())
		ShowError(_T("No AMD Processor is found!"), FALSE, 1);

	//Check if driver is installed or not
	if (QueryDrvService() < 0)
	{
		if (false == InstallDriver())
			ShowError(_T("Unable to install driver AMDRyzenMasterDriver.sys : Driver not found!"), FALSE, 1);
	}

	//Check if application is running on supported Processor?
	if (!IsSupportedProcessor())
	{
		ShowError(_T("Not Supported Processor!"), FALSE, 1);
	}
	
	try {
		Process_program_options(argc, argv);
	}
	catch (...)
	{
		ShowError(_T("Unable to Process the command!"), FALSE, 1);
	}

	return 0;
}