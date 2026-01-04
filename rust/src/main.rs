use std::env;
use std::ffi::CString;
use std::os::raw::{c_char, c_int};

#[cfg(not(windows))]
mod ld_device;

#[cfg(windows)]
extern "C" {
    fn run_sample_app(argc: c_int, argv: *mut *mut c_char) -> c_int;
}

#[cfg(windows)]
fn main() {
    let args: Vec<CString> = env::args()
        .map(|arg| CString::new(arg).expect("argument contains null byte"))
        .collect();
    let mut argv: Vec<*mut c_char> = args
        .iter()
        .map(|arg| arg.as_ptr() as *mut c_char)
        .collect();

    let exit_code = unsafe { run_sample_app(argv.len() as c_int, argv.as_mut_ptr()) };
    std::process::exit(exit_code);
}

#[cfg(not(windows))]
fn main() {
    let args: Vec<String> = env::args().collect();
    if args.iter().any(|arg| arg == "--ld-send") {
        if let Err(message) = ld_device::run(&args) {
            eprintln!("{message}");
            std::process::exit(1);
        }
        return;
    }

    eprintln!("This application only supports Windows targets unless using --ld-send.");
    std::process::exit(1);
}
