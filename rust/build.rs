use std::env;
use std::path::PathBuf;

fn parse_version(name: &str) -> Option<(u32, u32, u32, u32)> {
    let mut parts = name.split('.');
    let a = parts.next()?.parse().ok()?;
    let b = parts.next()?.parse().ok()?;
    let c = parts.next()?.parse().ok()?;
    let d = parts.next()?.parse().ok()?;
    if parts.next().is_some() {
        return None;
    }
    Some((a, b, c, d))
}

fn pick_latest_version_dir(root: &std::path::Path) -> Option<PathBuf> {
    let mut best: Option<(std::path::PathBuf, (u32, u32, u32, u32))> = None;
    let entries = std::fs::read_dir(root).ok()?;
    for entry in entries.flatten() {
        let path = entry.path();
        if !path.is_dir() {
            continue;
        }
        let name = match path.file_name().and_then(|n| n.to_str()) {
            Some(value) => value,
            None => continue,
        };
        let version = match parse_version(name) {
            Some(v) => v,
            None => continue,
        };
        if best.as_ref().map(|(_, b)| &version > b).unwrap_or(true) {
            best = Some((path, version));
        }
    }
    best.map(|(path, _)| path)
}

fn windows_sdk_include_dirs() -> Vec<PathBuf> {
    let mut dirs = Vec::new();
    let sdk_root = env::var("WindowsSdkDir")
        .map(PathBuf::from)
        .ok()
        .map(|p| {
            if p.join("Include").is_dir() {
                p.join("Include")
            } else {
                p
            }
        })
        .or_else(|| {
            env::var("ProgramFiles(x86)")
                .map(PathBuf::from)
                .ok()
                .map(|p| p.join("Windows Kits").join("10").join("Include"))
        });

    let sdk_root = match sdk_root {
        Some(root) if root.is_dir() => root,
        _ => return dirs,
    };

    let sdk_version = env::var("WindowsSDKVersion")
        .ok()
        .map(|v| v.trim_end_matches('\\').to_string());
    let version_dir = sdk_version
        .and_then(|v| {
            let candidate = sdk_root.join(v);
            if candidate.is_dir() {
                Some(candidate)
            } else {
                None
            }
        })
        .or_else(|| pick_latest_version_dir(&sdk_root));

    let version_dir = match version_dir {
        Some(dir) => dir,
        None => return dirs,
    };

    for subdir in ["um", "shared", "ucrt", "winrt"] {
        let path = version_dir.join(subdir);
        if path.is_dir() {
            dirs.push(path);
        }
    }

    dirs
}

fn msvc_include_dir() -> Option<PathBuf> {
    if let Ok(dir) = env::var("VCToolsInstallDir") {
        let include = PathBuf::from(dir).join("include");
        if include.is_dir() {
            return Some(include);
        }
    }

    let target = env::var("TARGET").unwrap_or_default();
    let arch = if target.contains("aarch64") {
        "arm64"
    } else if target.contains("i686") {
        "x86"
    } else {
        "x64"
    };

    let tool = cc::windows_registry::find_tool(arch, "cl.exe")?;
    for ancestor in tool.path().ancestors() {
        let include = ancestor.join("include");
        let bin = ancestor.join("bin");
        if include.is_dir() && bin.is_dir() {
            return Some(include);
        }
    }

    None
}

fn main() {
    if !cfg!(target_os = "windows") {
        return;
    }

    println!("cargo:rerun-if-changed=Platform.dll");
    println!("cargo:rerun-if-changed=Device.dll");
    embed_manifest::embed_manifest_file("app.manifest")
        .expect("failed to embed Windows app manifest");

    let manifest_dir = PathBuf::from(env::var("CARGO_MANIFEST_DIR").expect("CARGO_MANIFEST_DIR missing"));
    let repo_root = manifest_dir.join("..");
    println!(
        "cargo:rerun-if-changed={}",
        repo_root.join("src").join("telemetry.cpp").display()
    );
    println!(
        "cargo:rerun-if-changed={}",
        repo_root.join("src").join("Utility.cpp").display()
    );
    println!(
        "cargo:rerun-if-changed={}",
        repo_root.join("inc").join("Utility.hpp").display()
    );
    let local_sdk_include = repo_root.join("third_party").join("amd_ryzen_master_sdk").join("include");
    if !local_sdk_include.join("ICPUEx.h").exists() {
        panic!(
            "Local AMD Ryzen Master SDK headers not found at {}. Vendor them into third_party/amd_ryzen_master_sdk/include.",
            local_sdk_include.display()
        );
    }
    let sdk_include = local_sdk_include;

    let mut build = cc::Build::new();
    build
        .cpp(true)
        .flag_if_supported("/std:c++20")
        .flag_if_supported("/EHsc")
        .define("UNICODE", None)
        .define("_UNICODE", None)
        .include(repo_root.join("inc"))
        .include(sdk_include);

    if let Some(msvc_include) = msvc_include_dir() {
        build.include(msvc_include);
    }
    for dir in windows_sdk_include_dirs() {
        build.include(dir);
    }

    build
        .file(repo_root.join("src").join("telemetry.cpp"))
        .file(repo_root.join("src").join("Utility.cpp"))
        .compile("ryzenmaster_wrapper");

    println!("cargo:rustc-link-lib=Netapi32");
    println!("cargo:rustc-link-lib=User32");
    println!("cargo:rustc-link-lib=Shell32");
    println!("cargo:rustc-link-lib=Advapi32");
}
