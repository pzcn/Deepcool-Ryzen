use std::ffi::c_char;

#[no_mangle]
pub extern "C" fn deepcool_get_hid_data() -> *const c_char {
    static HID_DATA: &[u8] = b"HID sample data from Rust\0";
    HID_DATA.as_ptr() as *const c_char
}
