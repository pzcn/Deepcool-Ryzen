//===----------------------------------------------------------------------===
//=== Copyright (c) 2023-2025 Advanced Micro Devices, Inc.  All rights reserved.
//
//            Developed by: Advanced Micro Devices, Inc.

#pragma once
#include <windows.h>
#include <Shlobj.h>
#include <intrin.h>
#include <chrono>
#include <thread>
#include <setupapi.h>
#include <hidsdi.h>
#include <array>
#include <vector>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include "ICPUEx.h"
#include "IPlatform.h"
#include "IDeviceManager.h"
#include "IBIOSEx.h"

#include "Utility.hpp"

#pragma comment(lib, "Setupapi.lib")
#pragma comment(lib, "Hid.lib")

constexpr USHORT kFixedVid = 0x3633;
constexpr USHORT kFixedPid = 0x000A;

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

std::wstring FindHidDevicePath(USHORT vid, USHORT pid)
{
	GUID hidGuid;
	HidD_GetHidGuid(&hidGuid);
	HDEVINFO deviceInfo = SetupDiGetClassDevs(&hidGuid, NULL, NULL, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
	if (deviceInfo == INVALID_HANDLE_VALUE)
	{
		return L"";
	}

	SP_DEVICE_INTERFACE_DATA interfaceData = {};
	interfaceData.cbSize = sizeof(interfaceData);

	for (DWORD index = 0; SetupDiEnumDeviceInterfaces(deviceInfo, NULL, &hidGuid, index, &interfaceData); ++index)
	{
		DWORD requiredSize = 0;
		SetupDiGetDeviceInterfaceDetail(deviceInfo, &interfaceData, NULL, 0, &requiredSize, NULL);
		if (!requiredSize)
		{
			continue;
		}

		std::vector<BYTE> detailBuffer(requiredSize);
		auto detailData = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(detailBuffer.data());
		detailData->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);

		if (!SetupDiGetDeviceInterfaceDetail(deviceInfo, &interfaceData, detailData, requiredSize, NULL, NULL))
		{
			continue;
		}

		HANDLE handle = CreateFile(detailData->DevicePath, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
		if (handle == INVALID_HANDLE_VALUE)
		{
			continue;
		}

		HIDD_ATTRIBUTES attributes = {};
		attributes.Size = sizeof(attributes);
		BOOL matched = HidD_GetAttributes(handle, &attributes);
		CloseHandle(handle);

		if (matched && attributes.VendorID == vid && attributes.ProductID == pid)
		{
			std::wstring path(detailData->DevicePath);
			SetupDiDestroyDeviceInfoList(deviceInfo);
			return path;
		}
	}

	SetupDiDestroyDeviceInfoList(deviceInfo);
	return L"";
}

bool WriteHidPacket(HANDLE deviceHandle, const std::array<uint8_t, 64>& payload)
{
	DWORD bytesWritten = 0;
	BOOL status = WriteFile(deviceHandle, payload.data(), static_cast<DWORD>(payload.size()), &bytesWritten, NULL);
	if (!status || bytesWritten != payload.size())
	{
		return false;
	}
	return true;
}

bool InitUsbDevice(HANDLE& deviceHandle)
{
	deviceHandle = INVALID_HANDLE_VALUE;
	std::wstring devicePath = FindHidDevicePath(kFixedVid, kFixedPid);
	if (devicePath.empty())
	{
		return false;
	}

	deviceHandle = CreateFile(devicePath.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (deviceHandle == INVALID_HANDLE_VALUE)
	{
		return false;
	}

	std::array<uint8_t, 64> packet = {};
	packet[0] = 16;
	packet[1] = 104;
	packet[2] = 1;
	packet[3] = 1;

	std::array<uint8_t, 64> initPacket = packet;
	initPacket[4] = 2;
	initPacket[5] = 3;
	initPacket[6] = 1;
	initPacket[7] = 112;
	initPacket[8] = 22;
	if (!WriteHidPacket(deviceHandle, initPacket))
	{
		CloseHandle(deviceHandle);
		deviceHandle = INVALID_HANDLE_VALUE;
		return false;
	}

	initPacket[5] = 2;
	initPacket[7] = 111;
	if (!WriteHidPacket(deviceHandle, initPacket))
	{
		CloseHandle(deviceHandle);
		deviceHandle = INVALID_HANDLE_VALUE;
		return false;
	}

	return true;
}

bool SendUsbStatusPacket(HANDLE deviceHandle, int temperatureC, int powerW, int usagePercent)
{
	std::array<uint8_t, 64> packet = {};
	packet[0] = 16;
	packet[1] = 104;
	packet[2] = 1;
	packet[3] = 1;
	packet[4] = 11;
	packet[5] = 1;
	packet[6] = 2;
	packet[7] = 5;

	uint16_t powerInt = static_cast<uint16_t>(std::clamp(powerW, 0, 65535));
	packet[8] = static_cast<uint8_t>(powerInt >> 8);
	packet[9] = static_cast<uint8_t>(powerInt & 0xFF);

	packet[10] = 0;
	uint32_t tempBits = 0;
	float tempValue = static_cast<float>(temperatureC);
	std::memcpy(&tempBits, &tempValue, sizeof(tempBits));
	packet[11] = static_cast<uint8_t>((tempBits >> 24) & 0xFF);
	packet[12] = static_cast<uint8_t>((tempBits >> 16) & 0xFF);
	packet[13] = static_cast<uint8_t>((tempBits >> 8) & 0xFF);
	packet[14] = static_cast<uint8_t>(tempBits & 0xFF);

	uint8_t utilization = static_cast<uint8_t>(std::clamp(usagePercent, 0, 100));
	packet[15] = utilization;

	uint16_t checksum = 0;
	for (size_t i = 1; i <= 15; ++i)
	{
		checksum += packet[i];
	}
	packet[16] = static_cast<uint8_t>(checksum % 256);
	packet[17] = 22;

	return WriteHidPacket(deviceHandle, packet);
}

void StreamToConsoleAndUsb()
{
	MonitoringContext ctx;
	if (!InitMonitoringContext(ctx))
	{
		return;
	}

	HANDLE usbHandle = INVALID_HANDLE_VALUE;
	bool usbReady = InitUsbDevice(usbHandle);
	while (true)
	{
		double temperatureC = 0.0;
		double powerW = 0.0;
		double usagePercent = 0.0;

		auto tryReadTelemetry = [&](int maxAttempts) -> bool
		{
			for (int attempt = 1; attempt <= maxAttempts; ++attempt)
			{
				if (ReadCPUTelemetry(ctx.cpu, temperatureC, powerW, usagePercent))
				{
					if (attempt > 1)
					{
					}
					return true;
				}

				if (attempt < maxAttempts)
				{
					std::this_thread::sleep_for(std::chrono::seconds(1));
				}
			}
			return false;
		};

		bool telemetryOk = tryReadTelemetry(10);
		if (!telemetryOk)
		{
			std::this_thread::sleep_for(std::chrono::minutes(1));

			telemetryOk = tryReadTelemetry(10);
			if (!telemetryOk)
			{
				break;
			}
		}

		int tempRounded = static_cast<int>(std::lround(temperatureC));
		int powerRounded = static_cast<int>(std::lround(powerW));
		int usageRounded = static_cast<int>(std::lround(usagePercent));

		if (usbReady)
		{
			if (!SendUsbStatusPacket(usbHandle, tempRounded, powerRounded, usageRounded))
			{
				CloseHandle(usbHandle);
				usbHandle = INVALID_HANDLE_VALUE;
				usbReady = false;
			}
		}

		std::this_thread::sleep_for(std::chrono::seconds(1));
	}

	if (usbHandle != INVALID_HANDLE_VALUE)
	{
		CloseHandle(usbHandle);
	}
	CleanupMonitoringContext(ctx);
}

extern "C" int run_sample_app(int argc, CHAR **argv)
{
	(void)argc;
	(void)argv;
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
	
	StreamToConsoleAndUsb();

	return 0;
}
