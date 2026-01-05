use std::env;
use std::path::PathBuf;

fn main() {
    if !cfg!(target_os = "windows") {
        return;
    }

    println!("cargo:rerun-if-changed=Platform.dll");
    println!("cargo:rerun-if-changed=Device.dll");
    embed_manifest::embed_manifest_file("app.manifest")
        .expect("failed to embed Windows app manifest");

    let sdk_path = env::var("AMDRMMONITORSDKPATH").unwrap_or_else(|_| {
        panic!(
            "AMDRMMONITORSDKPATH is not set. Point it to the Ryzen Master Monitoring SDK root to build."
        )
    });
    let sdk_include = PathBuf::from(&sdk_path).join("include");

    cc::Build::new()
        .cpp(true)
        .flag_if_supported("/std:c++20")
        .flag_if_supported("/EHsc")
        .define("UNICODE", None)
        .define("_UNICODE", None)
        .include("../inc")
        .include(sdk_include)
        .file("../src/telemetry.cpp")
        .file("../src/Utility.cpp")
        .compile("ryzenmaster_wrapper");

    println!("cargo:rustc-link-lib=Netapi32");
    println!("cargo:rustc-link-lib=User32");
    println!("cargo:rustc-link-lib=Shell32");
    println!("cargo:rustc-link-lib=Advapi32");
}
