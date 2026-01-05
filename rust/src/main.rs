#[cfg(windows)]
mod windows_app {
    use std::ffi::OsStr;
    use std::io::{self, Write};
    use std::os::raw::{c_double, c_int};
    use std::os::windows::ffi::OsStrExt;
    use std::path::Path;
    use std::ptr;
    use std::thread;
    use std::time::Duration;

    use windows::core::{PCWSTR, PWSTR};
    use windows::Win32::Devices::DeviceAndDriverInstallation::{
        SetupDiDestroyDeviceInfoList, SetupDiEnumDeviceInterfaces, SetupDiGetClassDevsW,
        SetupDiGetDeviceInterfaceDetailW, DIGCF_DEVICEINTERFACE, DIGCF_PRESENT,
        SP_DEVICE_INTERFACE_DATA, SP_DEVICE_INTERFACE_DETAIL_DATA_W,
    };
    use windows::Win32::Devices::HumanInterfaceDevice::{HidD_GetAttributes, HidD_GetHidGuid, HIDD_ATTRIBUTES};
    use windows::Win32::Foundation::{
        CloseHandle, GetLastError, HANDLE, WAIT_OBJECT_0, ERROR_SERVICE_DOES_NOT_EXIST, GENERIC_READ, GENERIC_WRITE,
    };
    use windows::Win32::Storage::FileSystem::{
        CreateFileW, WriteFile, FILE_ATTRIBUTE_NORMAL, FILE_SHARE_READ, FILE_SHARE_WRITE, OPEN_EXISTING,
    };
    use windows::Win32::System::Services::{
        CloseServiceHandle, ControlService, CreateServiceW, DeleteService, OpenSCManagerW, OpenServiceW,
        QueryServiceStatus, RegisterServiceCtrlHandlerExW, SetServiceStatus, StartServiceCtrlDispatcherW,
        StartServiceW, SC_HANDLE,
        SC_MANAGER_CONNECT, SC_MANAGER_CREATE_SERVICE, SERVICE_ACCEPT_SHUTDOWN, SERVICE_ACCEPT_STOP,
        SERVICE_ALL_ACCESS, SERVICE_CONTROL_STOP, SERVICE_DEMAND_START, SERVICE_ERROR_NORMAL, SERVICE_QUERY_STATUS,
        SERVICE_RUNNING, SERVICE_START_PENDING, SERVICE_STATUS, SERVICE_STATUS_CURRENT_STATE, SERVICE_STATUS_HANDLE,
        SERVICE_STOPPED, SERVICE_STOP_PENDING, SERVICE_TABLE_ENTRYW, SERVICE_WIN32_OWN_PROCESS, SERVICE_START,
        SERVICE_STOP,
    };
    use windows::Win32::System::Threading::{CreateEventW, SetEvent, WaitForSingleObject};

    const K_FIXED_VID: u16 = 0x3633;
    const K_FIXED_PID: u16 = 0x000A;
    const HID_PACKET_SIZE: usize = 64;

    const PLATFORM_DLL_FILE: &str = "Platform.dll";
    const EMBEDDED_PLATFORM_DLL: &[u8] = include_bytes!(concat!(env!("CARGO_MANIFEST_DIR"), "/Platform.dll"));
    const DEVICE_DLL_FILE: &str = "Device.dll";
    const EMBEDDED_DEVICE_DLL: &[u8] = include_bytes!(concat!(env!("CARGO_MANIFEST_DIR"), "/Device.dll"));

    const SERVICE_NAME: &str = "RyzenMasterMonitor";
    const SERVICE_DISPLAY_NAME: &str = "Ryzen Master Monitor";

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

    static mut SERVICE_HANDLE: SERVICE_STATUS_HANDLE = SERVICE_STATUS_HANDLE(ptr::null_mut());
    static mut SERVICE_STOP_EVENT: HANDLE = HANDLE(ptr::null_mut());

    #[repr(C)]
    struct RMMonitorContext {
        _private: [u8; 0],
    }

    extern "C" {
        fn rm_monitor_set_sdk_path(path: *const u16);
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
                    let _ = CloseHandle(self.0);
                }
            }
        }
    }

    #[derive(Clone, Copy, Debug, PartialEq, Eq)]
    enum ServiceState {
        NotInstalled,
        Running,
        Stopped,
    }

    pub fn run() -> i32 {
        let args: Vec<String> = std::env::args().collect();
        if args.iter().any(|arg| arg == "--service") {
            return run_service();
        }
        if args.iter().any(|arg| arg == "--run") {
            return run_monitor_loop(None);
        }

        run_cli()
    }

    fn run_cli() -> i32 {
        loop {
            let state = match query_service_state() {
                Ok(state) => state,
                Err(err) => {
                    eprintln!("服务状态读取失败: {err}");
                    return 1;
                }
            };

            println!("服务状态：{}", service_state_label(state));
            let install_label = if state == ServiceState::NotInstalled { "安装" } else { "卸载" };
            let start_label = if state == ServiceState::Running { "停止" } else { "启动" };
            println!("1. {install_label}服务");
            println!("2. {start_label}服务");
            println!("3. 直接运行");
            println!("q. 退出");
            print!("请选择: ");
            let _ = io::stdout().flush();

            let mut input = String::new();
            if io::stdin().read_line(&mut input).is_err() {
                return 1;
            }
            match input.trim() {
                "1" => {
                    let result = if state == ServiceState::NotInstalled {
                        install_service()
                    } else {
                        uninstall_service()
                    };
                    if let Err(err) = result {
                        eprintln!("{err}");
                    }
                }
                "2" => {
                    let result = if state == ServiceState::Running {
                        stop_service()
                    } else if state == ServiceState::NotInstalled {
                        Err(String::from("服务未安装。"))
                    } else {
                        start_service()
                    };
                    if let Err(err) = result {
                        eprintln!("{err}");
                    }
                }
                "3" => {
                    return run_monitor_loop(None);
                }
                "q" | "Q" => break,
                _ => println!("无效输入。"),
            }

            println!();
        }

        0
    }

    fn run_monitor_loop(stop_event: Option<HANDLE>) -> i32 {
        let platform_dir = match ensure_platform_dll_available() {
            Some(dir) => dir,
            None => {
                eprintln!("ryzenmaster-monitor: failed to locate Platform.dll");
                return 1;
            }
        };
        std::env::set_var("AMDRMMONITORSDKPATH", &platform_dir);
        let wide_path = path_to_wide(&platform_dir);
        unsafe {
            rm_monitor_set_sdk_path(wide_path.as_ptr());
        }
        println!("ryzenmaster-monitor: starting");

        let mut raw_ctx: *mut RMMonitorContext = ptr::null_mut();
        let status = unsafe { rm_monitor_init(&mut raw_ctx) };
        if status != RM_STATUS_OK {
            let message = format!(
                "ryzenmaster-monitor: telemetry init failed: {} ({})",
                status_message(status),
                status
            );
            eprintln!("{message}");
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
                let message = format!(
                    "ryzenmaster-monitor: USB HID device not found (vid=0x{K_FIXED_VID:04x} pid=0x{K_FIXED_PID:04x})"
                );
                eprintln!("{message}");
                None
            }
        };

        loop {
            if stop_requested(stop_event) {
                break;
            }

            let telemetry = match read_telemetry_with_retries(ctx.ptr(), 10, stop_event) {
                Ok(values) => values,
                Err(status) => {
                    if stop_requested(stop_event) {
                        break;
                    }
                    let message = format!(
                        "ryzenmaster-monitor: telemetry read failed: {} ({})",
                        status_message(status),
                        status
                    );
                    eprintln!("{message}");
                    if wait_or_stop(stop_event, Duration::from_secs(60)) {
                        break;
                    }
                    match read_telemetry_with_retries(ctx.ptr(), 10, stop_event) {
                        Ok(values) => values,
                        Err(status) => {
                            if stop_requested(stop_event) {
                                break;
                            }
                            let message = format!(
                                "ryzenmaster-monitor: telemetry read failed after retry: {} ({})",
                                status_message(status),
                                status
                            );
                            eprintln!("{message}");
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

            if wait_or_stop(stop_event, Duration::from_secs(1)) {
                break;
            }
        }

        0
    }

    fn stop_requested(stop_event: Option<HANDLE>) -> bool {
        if let Some(handle) = stop_event {
            unsafe { WaitForSingleObject(handle, 0) == WAIT_OBJECT_0 }
        } else {
            false
        }
    }

    fn wait_or_stop(stop_event: Option<HANDLE>, duration: Duration) -> bool {
        if let Some(handle) = stop_event {
            let timeout_ms = duration.as_millis().min(u32::MAX as u128) as u32;
            unsafe { WaitForSingleObject(handle, timeout_ms) == WAIT_OBJECT_0 }
        } else {
            thread::sleep(duration);
            false
        }
    }

    fn service_state_label(state: ServiceState) -> &'static str {
        match state {
            ServiceState::Running => "运行中",
            ServiceState::Stopped => "未运行",
            ServiceState::NotInstalled => "未安装",
        }
    }

    fn ensure_platform_dll_available() -> Option<std::path::PathBuf> {
        let temp_dir = std::env::temp_dir().join("ryzenmaster-monitor");
        if let Err(err) = std::fs::create_dir_all(&temp_dir) {
            eprintln!("ryzenmaster-monitor: failed to create temp dir: {err}");
            return None;
        }

        let platform_path = temp_dir.join(PLATFORM_DLL_FILE);
        if !ensure_embedded_file(
            &platform_path,
            EMBEDDED_PLATFORM_DLL,
            PLATFORM_DLL_FILE,
        ) {
            return None;
        }

        let device_path = temp_dir.join(DEVICE_DLL_FILE);
        if !ensure_embedded_file(
            &device_path,
            EMBEDDED_DEVICE_DLL,
            DEVICE_DLL_FILE,
        ) {
            return None;
        }

        Some(temp_dir)
    }

    fn ensure_embedded_file(path: &Path, bytes: &[u8], label: &str) -> bool {
        let needs_write = match std::fs::metadata(path) {
            Ok(metadata) => metadata.len() != bytes.len() as u64,
            Err(_) => true,
        };

        if needs_write {
            if let Err(err) = std::fs::write(path, bytes) {
                eprintln!("ryzenmaster-monitor: failed to extract {label}: {err}");
                return false;
            }
        }
        true
    }

    fn query_service_state() -> Result<ServiceState, String> {
        unsafe {
            let manager = OpenSCManagerW(PCWSTR::null(), PCWSTR::null(), SC_MANAGER_CONNECT)
                .map_err(|err| format!("打开服务管理器失败: {err}"))?;
            let service = match open_service_if_exists(manager, SERVICE_QUERY_STATUS)? {
                Some(handle) => handle,
                None => {
                    let _ = CloseServiceHandle(manager);
                    return Ok(ServiceState::NotInstalled);
                }
            };

            let mut status = SERVICE_STATUS::default();
            let result = QueryServiceStatus(service, &mut status);
            let _ = CloseServiceHandle(service);
            let _ = CloseServiceHandle(manager);
            result.map_err(|err| format!("查询服务状态失败: {err}"))?;

            if status.dwCurrentState == SERVICE_RUNNING {
                Ok(ServiceState::Running)
            } else {
                Ok(ServiceState::Stopped)
            }
        }
    }

    fn install_service() -> Result<(), String> {
        unsafe {
            let manager = OpenSCManagerW(PCWSTR::null(), PCWSTR::null(), SC_MANAGER_CREATE_SERVICE)
                .map_err(|err| format!("打开服务管理器失败: {err}"))?;
            if let Some(handle) = open_service_if_exists(manager, SERVICE_QUERY_STATUS)? {
                let _ = CloseServiceHandle(handle);
                let _ = CloseServiceHandle(manager);
                return Err(String::from("服务已安装。"));
            }

            let bin_path = build_service_bin_path()?;
            let service_name = to_wide(SERVICE_NAME);
            let display_name = to_wide(SERVICE_DISPLAY_NAME);
            let service = CreateServiceW(
                manager,
                PCWSTR(service_name.as_ptr()),
                PCWSTR(display_name.as_ptr()),
                SERVICE_ALL_ACCESS,
                SERVICE_WIN32_OWN_PROCESS,
                SERVICE_DEMAND_START,
                SERVICE_ERROR_NORMAL,
                PCWSTR(bin_path.as_ptr()),
                PCWSTR::null(),
                None,
                PCWSTR::null(),
                PCWSTR::null(),
                PCWSTR::null(),
            )
            .map_err(|err| format!("安装服务失败: {err}"))?;

            let _ = CloseServiceHandle(service);
            let _ = CloseServiceHandle(manager);
        }

        println!("服务已安装。");
        Ok(())
    }

    fn uninstall_service() -> Result<(), String> {
        unsafe {
            let manager = OpenSCManagerW(PCWSTR::null(), PCWSTR::null(), SC_MANAGER_CONNECT)
                .map_err(|err| format!("打开服务管理器失败: {err}"))?;
            let service = match open_service_if_exists(manager, SERVICE_ALL_ACCESS)? {
                Some(handle) => handle,
                None => {
                    let _ = CloseServiceHandle(manager);
                    return Err(String::from("服务未安装。"));
                }
            };

            let mut status = SERVICE_STATUS::default();
            if QueryServiceStatus(service, &mut status).is_ok() && status.dwCurrentState == SERVICE_RUNNING {
                let _ = ControlService(service, SERVICE_CONTROL_STOP, &mut status);
                wait_service_state(service, SERVICE_STOPPED, Duration::from_secs(10));
            }

            DeleteService(service).map_err(|err| format!("卸载服务失败: {err}"))?;
            let _ = CloseServiceHandle(service);
            let _ = CloseServiceHandle(manager);
        }

        println!("服务已卸载。");
        Ok(())
    }

    fn start_service() -> Result<(), String> {
        unsafe {
            let manager = OpenSCManagerW(PCWSTR::null(), PCWSTR::null(), SC_MANAGER_CONNECT)
                .map_err(|err| format!("打开服务管理器失败: {err}"))?;
            let service = match open_service_if_exists(manager, SERVICE_START | SERVICE_QUERY_STATUS)? {
                Some(handle) => handle,
                None => {
                    let _ = CloseServiceHandle(manager);
                    return Err(String::from("服务未安装。"));
                }
            };

            StartServiceW(service, None).map_err(|err| format!("启动服务失败: {err}"))?;
            if !wait_service_state(service, SERVICE_RUNNING, Duration::from_secs(10)) {
                let _ = CloseServiceHandle(service);
                let _ = CloseServiceHandle(manager);
                return Err(String::from("服务启动超时。"));
            }

            let _ = CloseServiceHandle(service);
            let _ = CloseServiceHandle(manager);
        }

        println!("服务已启动。");
        Ok(())
    }

    fn stop_service() -> Result<(), String> {
        unsafe {
            let manager = OpenSCManagerW(PCWSTR::null(), PCWSTR::null(), SC_MANAGER_CONNECT)
                .map_err(|err| format!("打开服务管理器失败: {err}"))?;
            let service = match open_service_if_exists(manager, SERVICE_STOP | SERVICE_QUERY_STATUS)? {
                Some(handle) => handle,
                None => {
                    let _ = CloseServiceHandle(manager);
                    return Err(String::from("服务未安装。"));
                }
            };

            let mut status = SERVICE_STATUS::default();
            if QueryServiceStatus(service, &mut status).is_ok() && status.dwCurrentState == SERVICE_STOPPED {
                let _ = CloseServiceHandle(service);
                let _ = CloseServiceHandle(manager);
                println!("服务已停止。");
                return Ok(());
            }

            ControlService(service, SERVICE_CONTROL_STOP, &mut status)
                .map_err(|err| format!("停止服务失败: {err}"))?;
            if !wait_service_state(service, SERVICE_STOPPED, Duration::from_secs(10)) {
                let _ = CloseServiceHandle(service);
                let _ = CloseServiceHandle(manager);
                return Err(String::from("服务停止超时。"));
            }

            let _ = CloseServiceHandle(service);
            let _ = CloseServiceHandle(manager);
        }

        println!("服务已停止。");
        Ok(())
    }

    fn wait_service_state(service: SC_HANDLE, desired: SERVICE_STATUS_CURRENT_STATE, timeout: Duration) -> bool {
        let mut elapsed_ms = 0u32;
        let timeout_ms = timeout.as_millis().min(u32::MAX as u128) as u32;
        while elapsed_ms < timeout_ms {
            let mut status = SERVICE_STATUS::default();
            if unsafe { QueryServiceStatus(service, &mut status).is_ok() } && status.dwCurrentState == desired {
                return true;
            }
            thread::sleep(Duration::from_millis(250));
            elapsed_ms = elapsed_ms.saturating_add(250);
        }
        false
    }

    fn open_service_if_exists(manager: SC_HANDLE, access: u32) -> Result<Option<SC_HANDLE>, String> {
        let service_name = to_wide(SERVICE_NAME);
        match unsafe { OpenServiceW(manager, PCWSTR(service_name.as_ptr()), access) } {
            Ok(handle) => Ok(Some(handle)),
            Err(err) => {
                let last_error = unsafe { GetLastError() };
                if last_error == ERROR_SERVICE_DOES_NOT_EXIST {
                    Ok(None)
                } else {
                    Err(format!("打开服务失败: {err}"))
                }
            }
        }
    }

    fn build_service_bin_path() -> Result<Vec<u16>, String> {
        let exe_path = std::env::current_exe().map_err(|err| format!("获取程序路径失败: {err}"))?;
        let mut wide = Vec::new();
        wide.push('"' as u16);
        wide.extend(exe_path.as_os_str().encode_wide());
        wide.push('"' as u16);
        wide.push(' ' as u16);
        wide.extend(OsStr::new("--service").encode_wide());
        wide.push(0);
        Ok(wide)
    }

    fn to_wide(value: &str) -> Vec<u16> {
        OsStr::new(value).encode_wide().chain(std::iter::once(0)).collect()
    }

    fn path_to_wide(path: &Path) -> Vec<u16> {
        path.as_os_str().encode_wide().chain(std::iter::once(0)).collect()
    }

    fn run_service() -> i32 {
        let service_name = to_wide(SERVICE_NAME);
        let table = [
            SERVICE_TABLE_ENTRYW {
                lpServiceName: PWSTR(service_name.as_ptr() as *mut _),
                lpServiceProc: Some(service_main),
            },
            SERVICE_TABLE_ENTRYW::default(),
        ];

        let result = unsafe { StartServiceCtrlDispatcherW(table.as_ptr()) };
        if let Err(err) = result {
            eprintln!("服务启动失败: {err}");
            return 1;
        }
        0
    }

    unsafe extern "system" fn service_main(_argc: u32, _argv: *mut PWSTR) {
        let service_name = to_wide(SERVICE_NAME);
        let handle = match RegisterServiceCtrlHandlerExW(
            PCWSTR(service_name.as_ptr()),
            Some(service_control_handler),
            None,
        ) {
            Ok(handle) => handle,
            Err(_) => {
                return;
            }
        };
        SERVICE_HANDLE = handle;
        update_service_status(SERVICE_START_PENDING, 3000);

        let stop_event = match CreateEventW(None, true, false, PCWSTR::null()) {
            Ok(event) => event,
            Err(_) => {
                update_service_status(SERVICE_STOPPED, 0);
                return;
            }
        };
        SERVICE_STOP_EVENT = stop_event;
        update_service_status(SERVICE_RUNNING, 0);

        let _ = run_monitor_loop(Some(stop_event));

        update_service_status(SERVICE_STOPPED, 0);
        let _ = CloseHandle(stop_event);
    }

    unsafe extern "system" fn service_control_handler(
        control: u32,
        _event_type: u32,
        _event_data: *mut core::ffi::c_void,
        _context: *mut core::ffi::c_void,
    ) -> u32 {
        if control == SERVICE_CONTROL_STOP {
            update_service_status(SERVICE_STOP_PENDING, 3000);
            let stop_event = SERVICE_STOP_EVENT;
            if !stop_event.is_invalid() {
                let _ = SetEvent(stop_event);
            }
        }
        0
    }

    unsafe fn update_service_status(state: SERVICE_STATUS_CURRENT_STATE, wait_hint_ms: u32) {
        let controls = if state == SERVICE_START_PENDING || state == SERVICE_STOP_PENDING {
            0
        } else {
            SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN
        };
        let status = SERVICE_STATUS {
            dwServiceType: SERVICE_WIN32_OWN_PROCESS,
            dwCurrentState: state,
            dwControlsAccepted: controls,
            dwWin32ExitCode: 0,
            dwServiceSpecificExitCode: 0,
            dwCheckPoint: 0,
            dwWaitHint: wait_hint_ms,
        };
        let _ = SetServiceStatus(SERVICE_HANDLE, &status);
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

    fn read_telemetry_with_retries(ctx: *mut RMMonitorContext, max_attempts: u32, stop_event: Option<HANDLE>) -> Result<(f64, f64, f64), i32> {
        let mut last_status = RM_STATUS_READ_FAILED;
        for attempt in 1..=max_attempts {
            match read_telemetry(ctx) {
                Ok(values) => return Ok(values),
                Err(status) => {
                    last_status = status;
                }
            }
            if attempt < max_attempts {
                if wait_or_stop(stop_event, Duration::from_secs(1)) {
                    return Err(last_status);
                }
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

                let _ = CloseHandle(handle);
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
