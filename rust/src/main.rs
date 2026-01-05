use std::os::raw::{c_char, c_int};

#[cfg(windows)]
extern "C" {
    fn run_sample_app(argc: c_int, argv: *mut *mut c_char) -> c_int;
}

#[cfg(windows)]
fn main() {
    let exit_code = unsafe { run_sample_app(0, std::ptr::null_mut()) };
    std::process::exit(exit_code);
}

#[cfg(not(windows))]
fn main() {
    std::process::exit(1);
}
