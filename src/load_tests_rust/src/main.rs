use std::ffi::{CString, CStr};
use std::ffi::c_void;
use std::os::raw::c_char;
//use std::time;
use std::time::{Instant};
use std::thread;
use std::io;
use std::fs::{self};
use std::path::Path;
use std::path::PathBuf;
use std::vec::Vec;
use std::env;
extern crate ini;
use ini::Ini;
extern crate rand;
use rand::Rng;
extern crate hdrhistogram;
use hdrhistogram::Histogram;

extern {
    fn Aktualizr_create_from_path(path: *const c_char) -> *const c_void;
    fn Aktualizr_initialize(a: *const c_void) -> i32;
    fn Aktualizr_updates_check(a: *const c_void) -> *const c_void;
    fn Aktualizr_updates_free(a: *const c_void);
    fn Aktualizr_destroy(a: *const c_void);
}

// USING:
// Provisions generation:
// cargo run load-tests-rust libaktualizr-c_path meta_dir creds_path devices_num gateway
// Updates checking:
// cargo run load-tests-rust meta_dir

fn main() -> io::Result<()> {
    let args: Vec<String> = env::args().collect();

    if args.len() == 6 {
        let devices_count: u32 = args[4].parse().unwrap();

        _provisioning(
            /*meta_dir*/ &args[2],
            /*creds_path*/ &args[3],
            devices_count,
            /*gateway*/ &args[5],
            /*id_size*/ 16);
        _check_updates(
            /*meta_dir*/ &args[2],
            /*attempts_count*/ 1,
            /*print_statistics*/ false).expect("check_updates failed");
    }

    else if args.len() == 3 {
        _check_updates(
            /*meta_dir*/ &args[2],
            /*attempts_count*/ 20,
            /*print_statistics*/ true).expect("check_updates failed");
    }

    Ok(())
}

fn _generate_id(id_size: u32) -> String {
    let mut generator = rand::thread_rng();
    let mut unique_id = String::from("");
    for _x in 0..id_size
    {
        let n1: u8 = generator.gen();
        let formatted = format!("{:02x}", n1);
        unique_id.insert_str(0, &formatted);
    }
    return unique_id;
}

fn _provisioning(meta_path: &str, creds_path: &str,
    devices_count: u32, gateway: &str, id_size: u32) {

    println!("Provisions generation started");

    for _device in 0..devices_count
    {
        let unique_id = _generate_id(id_size);

        let mut device_dir = PathBuf::from(meta_path);
        device_dir.push(unique_id.clone());
        let mut sota_file_path = device_dir.clone();
        fs::create_dir(device_dir).expect("could not create device dir");

        let storage_path = sota_file_path.clone();

        let mut https_gateway = String::from("https://");
        https_gateway.push_str(gateway);

        let mut config = Ini::new();

        config.with_section(Some("tls".to_owned()))
        .set("server", https_gateway.as_str());

        config.with_section(Some("provision".to_owned()))
        .set("server", https_gateway.as_str())
        .set("provision_path", creds_path)
        .set("primary_ecu_serial", unique_id.as_str());

        config.with_section(Some("pacman".to_owned())).set("type", "none");

        config.with_section(Some("storage".to_owned()))
        .set("path", storage_path.into_os_string().into_string().unwrap())
        .set("type", "sqlite");

        sota_file_path.push("sota");
        sota_file_path.set_extension("toml");
        config.write_to_file( sota_file_path ).unwrap();
    }
    println!("Provisions generation completed");
}

fn _check_updates(meta_path: &str, attempts_count: u32,
    print_statistics: bool) -> io::Result<()> {

    println!("Updates checking started");

    let path = Path::new(meta_path);

    let mut thread_handles = Vec::new();
    let mut device_number = 0;
    if path.is_dir() {
        for entry in fs::read_dir(path)? {
            let entry = entry?;
            let mut path = entry.path();
            path.push("sota");
            path.set_extension("toml");
            let config_path = CString::new(path.to_str().unwrap()).expect("CString::new failed");

            let handle = thread::spawn(move ||
                _worker(device_number, &config_path, attempts_count, print_statistics));
            thread_handles.push(handle);

            device_number = device_number + 1;
        }
    }

    let mut overall_hist =
        Histogram::<u64>::new_with_bounds(1, 60 * 60 * 1000, 2).unwrap();
    let mut overall_initial_hist = overall_hist.clone();

    for handle in thread_handles
    {
        let (initial_hist, hist) = handle.join().unwrap();

        if print_statistics {
            overall_hist.add(hist).expect("histogram.add failed");
            overall_initial_hist.add(initial_hist).expect("histogram.add failed");
        }
    }

    if print_statistics {
        println!("");
        _print_statistics("initial histogram", overall_initial_hist);
        println!("");
        _print_statistics("performance histogram", overall_hist);
        println!("");
    }

    println!("Updates checking completed");
    Ok(())
}

fn _worker(device_number: u32, config_path: &CStr,
    attempts_count: u32, calculate_statistics: bool)
    -> (Histogram::<u64>, Histogram::<u64>)
{
    let aktualizr_handle = unsafe { Aktualizr_create_from_path(config_path.as_ptr()) };
    let init_result = unsafe { Aktualizr_initialize(aktualizr_handle) };
    println!("device_number {:2}, initialization result = {}", device_number, init_result);

    let mut hist =
        Histogram::<u64>::new_with_bounds(1, 10000, 2).expect("Histogram creation failed");
    let mut initial_hist = hist.clone();

    for _attempt in 0..attempts_count
    {
        let start = Instant::now();
        let updates = unsafe { Aktualizr_updates_check(aktualizr_handle) };
        let duration = start.elapsed();
        println!("device_number {:2}, attempt {:2}, duration = {:5?} ms",
            device_number, _attempt, duration.as_millis() as u32);

        if calculate_statistics
        {
            if  _attempt == 0 {
                initial_hist.record(duration.as_millis() as u64).expect("Duration out of range");
            }
            else {
                hist.record(duration.as_millis() as u64).expect("Duration out of range");
            }
        }

        unsafe { Aktualizr_updates_free(updates); }

        //thread::sleep(time::Duration::from_millis(3000));
    }

    unsafe { Aktualizr_destroy(aktualizr_handle) };

    return (initial_hist, hist);
}

fn _print_statistics(histogram_name: &str, histogram: Histogram::<u64>)
{
    println!("# of {} samples: {}", histogram_name, histogram.len());
    for percentile in (0 .. 110).step_by(10) {
        println!("{:3?}'th percentile of {}: {:5} ms",
            percentile, histogram_name,
            histogram.value_at_quantile(percentile as f64 * 0.01));
    }
}