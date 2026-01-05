#[cfg(windows)]
mod windows_app {
    use std::os::raw::{c_double, c_int};
    use std::ptr;
    use std::thread;
    use std::time::Duration;

    use windows::core::PCWSTR;
    use windows::Win32::Devices::DeviceAndDriverInstallation::{
        SetupDiDestroyDeviceInfoList, SetupDiEnumDeviceInterfaces, SetupDiGetClassDevsW,
        SetupDiGetDeviceInterfaceDetailW, DIGCF_DEVICEINTERFACE, DIGCF_PRESENT,
        SP_DEVICE_INTERFACE_DATA, SP_DEVICE_INTERFACE_DETAIL_DATA_W,
    };
    use windows::Win32::Devices::HumanInterfaceDevice::{HidD_GetAttributes, HidD_GetHidGuid, HIDD_ATTRIBUTES};
    use windows::Win32::Foundation::{CloseHandle, HANDLE, GENERIC_READ, GENERIC_WRITE};
    use windows::Win32::Storage::FileSystem::{
        CreateFileW, WriteFile, FILE_ATTRIBUTE_NORMAL, FILE_SHARE_READ, FILE_SHARE_WRITE, OPEN_EXISTING,
    };

    const K_FIXED_VID: u16 = 0x3633;
    const K_FIXED_PID: u16 = 0x000A;
    const HID_PACKET_SIZE: usize = 64;

    const RM_STATUS_OK: i32 = 0;
    const RM_STATUS_INVALID_ARG: i32 = 1;
    const RM_STATUS_NOT_ADMIN: i32 = 2;
    const RM_STATUS_UNSUPPORTED_OS: i32 = 3;
    const RM_STATUS_NOT_AMD: i32 = 4;
    const RM_STATUS_DRIVER: i32 = 5;
    const RM_STATUS_UNSUPPORTED_CPU: i32 = 6;
    const RM_STATUS_ALLOC_FAILED: i32 = 7;
    const RM_STATUS_SDK_INIT_FAILED: i32 = 8;
    const RM_STATUS_READ_FAILED: i32 = 9;

    #[repr(C)]
    struct RMMonitorContext {
        _private: [u8; 0],
    }

    extern "C" {
        fn rm_monitor_init(out_ctx: *mut *mut RMMonitorContext) -> c_int;
        fn rm_monitor_read(
            ctx: *mut RMMonitorContext,
            temp_c: *mut c_double,
            power_w: *mut c_double,
            usage_percent: *mut c_double,
        ) -> c_int;
        fn rm_monitor_shutdown(ctx: *mut RMMonitorContext);
    }

    struct MonitorContext(*mut RMMonitorContext);

    impl MonitorContext {
        fn ptr(&self) -> *mut RMMonitorContext {
            self.0
        }
    }

    impl Drop for MonitorContext {
        fn drop(&mut self) {
            unsafe {
                if !self.0.is_null() {
                    rm_monitor_shutdown(self.0);
                }
            }
        }
    }

    struct HidHandle(HANDLE);

    impl Drop for HidHandle {
        fn drop(&mut self) {
            unsafe {
                if !self.0.is_invalid() {
                    CloseHandle(self.0);
                }
            }
        }
    }

    pub fn run() -> i32 {
        println!("ryzenmaster-monitor: starting");

        let mut raw_ctx: *mut RMMonitorContext = ptr::null_mut();
        let status = unsafe { rm_monitor_init(&mut raw_ctx) };
        if status != RM_STATUS_OK {
            eprintln!(
                "ryzenmaster-monitor: telemetry init failed: {} ({})",
                status_message(status),
                status
            );
            return 1;
        }

        let ctx = MonitorContext(raw_ctx);
        println!("ryzenmaster-monitor: telemetry ready");

        let mut hid = match open_hid_device(K_FIXED_VID, K_FIXED_PID) {
            Some(handle) => {
                if init_hid_device(handle.0) {
                    println!("ryzenmaster-monitor: USB HID ready");
                    Some(handle)
                } else {
                    eprintln!("ryzenmaster-monitor: USB HID init failed, running without USB output");
                    None
                }
            }
            None => {
                eprintln!(
                    "ryzenmaster-monitor: USB HID device not found (vid=0x{K_FIXED_VID:04x} pid=0x{K_FIXED_PID:04x})"
                );
                None
            }
        };

        loop {
            let telemetry = match read_telemetry_with_retries(ctx.ptr(), 10) {
                Ok(values) => values,
                Err(status) => {
                    eprintln!(
                        "ryzenmaster-monitor: telemetry read failed: {} ({}), retrying in 60s",
                        status_message(status),
                        status
                    );
                    thread::sleep(Duration::from_secs(60));
                    match read_telemetry_with_retries(ctx.ptr(), 10) {
                        Ok(values) => values,
                        Err(status) => {
                            eprintln!(
                                "ryzenmaster-monitor: telemetry read failed after retry: {} ({})",
                                status_message(status),
                                status
                            );
                            return 1;
                        }
                    }
                }
            };

            let temp_rounded = telemetry.0.round() as i32;
            let power_rounded = telemetry.1.round() as i32;
            let usage_rounded = telemetry.2.round() as i32;

            if let Some(handle) = hid.as_ref() {
                if !send_status_packet(handle.0, temp_rounded, power_rounded, usage_rounded) {
                    eprintln!("ryzenmaster-monitor: USB HID write failed, disabling USB output");
                    hid = None;
                }
            }

            thread::sleep(Duration::from_secs(1));
        }
    }

    fn status_message(code: i32) -> &'static str {
        match code {
            RM_STATUS_OK => "ok",
            RM_STATUS_INVALID_ARG => "invalid argument",
            RM_STATUS_NOT_ADMIN => "admin privileges required",
            RM_STATUS_UNSUPPORTED_OS => "unsupported Windows version",
            RM_STATUS_NOT_AMD => "non-AMD CPU detected",
            RM_STATUS_DRIVER => "Ryzen Master driver missing or failed to start",
            RM_STATUS_UNSUPPORTED_CPU => "unsupported CPU model",
            RM_STATUS_ALLOC_FAILED => "allocation failure",
            RM_STATUS_SDK_INIT_FAILED => "SDK initialization failed",
            RM_STATUS_READ_FAILED => "telemetry read failed",
            _ => "unknown error",
        }
    }

    fn read_telemetry(ctx: *mut RMMonitorContext) -> Result<(f64, f64, f64), i32> {
        let mut temperature = 0.0;
        let mut power = 0.0;
        let mut usage = 0.0;
        let status = unsafe { rm_monitor_read(ctx, &mut temperature, &mut power, &mut usage) };
        if status != RM_STATUS_OK {
            return Err(status);
        }
        Ok((temperature, power, usage))
    }

    fn read_telemetry_with_retries(ctx: *mut RMMonitorContext, max_attempts: u32) -> Result<(f64, f64, f64), i32> {
        let mut last_status = RM_STATUS_READ_FAILED;
        for attempt in 1..=max_attempts {
            match read_telemetry(ctx) {
                Ok(values) => return Ok(values),
                Err(status) => {
                    last_status = status;
                }
            }
            if attempt < max_attempts {
                thread::sleep(Duration::from_secs(1));
            }
        }
        Err(last_status)
    }

    fn open_hid_device(vid: u16, pid: u16) -> Option<HidHandle> {
        unsafe {
            let hid_guid = HidD_GetHidGuid();

            let device_info = match SetupDiGetClassDevsW(
                Some(&hid_guid as *const _),
                PCWSTR::null(),
                None,
                DIGCF_PRESENT | DIGCF_DEVICEINTERFACE,
            ) {
                Ok(handle) => handle,
                Err(_) => return None,
            };
            if device_info.is_invalid() {
                return None;
            }

            let mut interface_data = SP_DEVICE_INTERFACE_DATA::default();
            interface_data.cbSize = std::mem::size_of::<SP_DEVICE_INTERFACE_DATA>() as u32;

            let mut index = 0;
            loop {
                let ok = SetupDiEnumDeviceInterfaces(
                    device_info,
                    None,
                    &hid_guid,
                    index,
                    &mut interface_data,
                )
                .is_ok();
                if !ok {
                    break;
                }

                let mut required_size = 0u32;
                let _ = SetupDiGetDeviceInterfaceDetailW(
                    device_info,
                    &interface_data,
                    None,
                    0,
                    Some(&mut required_size),
                    None,
                );
                if required_size == 0 {
                    index += 1;
                    continue;
                }

                let mut buffer = vec![0u8; required_size as usize];
                let detail = buffer.as_mut_ptr() as *mut SP_DEVICE_INTERFACE_DETAIL_DATA_W;
                (*detail).cbSize = std::mem::size_of::<SP_DEVICE_INTERFACE_DETAIL_DATA_W>() as u32;

                let ok_detail = SetupDiGetDeviceInterfaceDetailW(
                    device_info,
                    &interface_data,
                    Some(detail),
                    required_size,
                    None,
                    None,
                )
                .is_ok();
                if !ok_detail {
                    index += 1;
                    continue;
                }

                let path_ptr = (*detail).DevicePath.as_ptr();
                let handle = match CreateFileW(
                    PCWSTR::from_raw(path_ptr),
                    GENERIC_READ.0 | GENERIC_WRITE.0,
                    FILE_SHARE_READ | FILE_SHARE_WRITE,
                    None,
                    OPEN_EXISTING,
                    FILE_ATTRIBUTE_NORMAL,
                    HANDLE::default(),
                ) {
                    Ok(handle) => handle,
                    Err(_) => {
                        index += 1;
                        continue;
                    }
                };

                let mut attributes = HIDD_ATTRIBUTES::default();
                attributes.Size = std::mem::size_of::<HIDD_ATTRIBUTES>() as u32;
                let matched = HidD_GetAttributes(handle, &mut attributes).as_bool();
                if matched && attributes.VendorID == vid && attributes.ProductID == pid {
                    let _ = SetupDiDestroyDeviceInfoList(device_info);
                    return Some(HidHandle(handle));
                }

                CloseHandle(handle);
                index += 1;
            }

            let _ = SetupDiDestroyDeviceInfoList(device_info);
            None
        }
    }

    fn write_hid_packet(handle: HANDLE, payload: &[u8; HID_PACKET_SIZE]) -> bool {
        let mut bytes_written = 0u32;
        unsafe {
            let ok = WriteFile(handle, Some(payload), Some(&mut bytes_written), None).is_ok();
            ok && bytes_written as usize == payload.len()
        }
    }

    fn init_hid_device(handle: HANDLE) -> bool {
        let mut packet = [0u8; HID_PACKET_SIZE];
        packet[0] = 16;
        packet[1] = 104;
        packet[2] = 1;
        packet[3] = 1;

        let mut init_packet = packet;
        init_packet[4] = 2;
        init_packet[5] = 3;
        init_packet[6] = 1;
        init_packet[7] = 112;
        init_packet[8] = 22;
        if !write_hid_packet(handle, &init_packet) {
            return false;
        }

        init_packet[5] = 2;
        init_packet[7] = 111;
        write_hid_packet(handle, &init_packet)
    }

    fn send_status_packet(handle: HANDLE, temperature_c: i32, power_w: i32, usage_percent: i32) -> bool {
        let mut packet = [0u8; HID_PACKET_SIZE];
        packet[0] = 16;
        packet[1] = 104;
        packet[2] = 1;
        packet[3] = 1;
        packet[4] = 11;
        packet[5] = 1;
        packet[6] = 2;
        packet[7] = 5;

        let power_int = power_w.clamp(0, 65535) as u16;
        packet[8] = (power_int >> 8) as u8;
        packet[9] = (power_int & 0xFF) as u8;

        packet[10] = 0;
        let temp_bits = (temperature_c as f32).to_bits();
        packet[11] = ((temp_bits >> 24) & 0xFF) as u8;
        packet[12] = ((temp_bits >> 16) & 0xFF) as u8;
        packet[13] = ((temp_bits >> 8) & 0xFF) as u8;
        packet[14] = (temp_bits & 0xFF) as u8;

        let utilization = usage_percent.clamp(0, 100) as u8;
        packet[15] = utilization;

        let mut checksum: u16 = 0;
        for value in &packet[1..=15] {
            checksum += *value as u16;
        }
        packet[16] = (checksum % 256) as u8;
        packet[17] = 22;

        write_hid_packet(handle, &packet)
    }
}

#[cfg(windows)]
fn main() {
    std::process::exit(windows_app::run());
}

#[cfg(not(windows))]
fn main() {
    std::process::exit(1);
}
