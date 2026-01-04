fn main() {
    if !cfg!(target_os = "windows") {
        return;
    }

    cc::Build::new()
        .cpp(true)
        .flag_if_supported("/std:c++17")
        .include("../inc")
        .file("../src/sampleApp.cpp")
        .file("../src/Utility.cpp")
        .compile("ryzenmaster_wrapper");
}
