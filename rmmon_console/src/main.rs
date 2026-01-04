use anyhow::{anyhow, Result};
use libloading::{Library, Symbol};
use std::{thread::sleep, time::Duration};

#[repr(C)]
#[derive(Debug, Copy, Clone)]
struct RmSnapshot {
    ppt_power_w: f32,
    temp_c: f32,
    usage_pct: f32,
    flags: u32,
}

type RmInit = unsafe extern "C" fn() -> i32;
type RmRead = unsafe extern "C" fn(*mut RmSnapshot) -> i32;
type RmShutdown = unsafe extern "C" fn();

fn fmt3(v: i32) -> String {
    let mut x = v;
    if x < 0 {
        x = 0;
    }
    if x > 999 {
        x = 999;
    }
    format!("{:03}", x)
}

fn main() -> Result<()> {
    let exe_path = std::env::current_exe()
        .map_err(|e| anyhow!("locate exe failed: {e}"))?;
    let exe_dir = exe_path
        .parent()
        .ok_or_else(|| anyhow!("locate exe directory failed"))?;
    let dll_path = exe_dir.join("rmmon_wrapper.dll");
    let lib = unsafe { Library::new(&dll_path) }
        .map_err(|e| anyhow!("load dll failed: {e}"))?;

    let rm_init: Symbol<RmInit> = unsafe { lib.get(b"rm_init\0") }?;
    let rm_read: Symbol<RmRead> = unsafe { lib.get(b"rm_read\0") }?;
    let rm_shutdown: Symbol<RmShutdown> = unsafe { lib.get(b"rm_shutdown\0") }?;

    let rc = unsafe { rm_init() };
    if rc != 0 {
        return Err(anyhow!("rm_init failed: {rc}"));
    }

    loop {
        let mut snap = RmSnapshot {
            ppt_power_w: 0.0,
            temp_c: 0.0,
            usage_pct: 0.0,
            flags: 0,
        };

        let rc = unsafe { rm_read(&mut snap as *mut RmSnapshot) };
        if rc == 0 && (snap.flags & 1) != 0 {
            let p = fmt3(snap.ppt_power_w.round() as i32);
            let t = fmt3(snap.temp_c.round() as i32);
            let u = fmt3(snap.usage_pct.round() as i32);

            println!("PPT={}W  TEMP={}C  USAGE={}%", p, t, u);
        } else {
            println!("rm_read failed: {rc}");
        }

        sleep(Duration::from_secs(1));
    }

    #[allow(unreachable_code)]
    {
        unsafe { rm_shutdown() };
        Ok(())
    }
}
