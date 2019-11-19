use std::ffi::{CString, CStr};
use std::ffi::c_void;
use std::os::raw::c_char;
use std::time;
use std::time::Instant;
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
    let checking_start_time = Instant::now();
    let histogram_upper_bound = 1500;

    let mut thread_handles = Vec::new();
    let mut device_number = 0;
    let mut upper_bound_overflow = 0;

    if path.is_dir() {
        for entry in fs::read_dir(path)? {
            let entry = entry?;
            let mut path = entry.path();
            path.push("sota");
            path.set_extension("toml");
            let config_path = CString::new(path.to_str().unwrap()).expect("CString::new failed");

            let handle = thread::spawn(move ||
                _worker(device_number, &config_path, checking_start_time,
                    attempts_count, print_statistics, histogram_upper_bound));
            thread_handles.push(handle);

            device_number = device_number + 1;
        }
    }

    let mut overall_hist =
        Histogram::<u32>::new_with_bounds(1, histogram_upper_bound as u64, 2).unwrap();
    let mut overall_initial_hist = overall_hist.clone();

    for handle in thread_handles
    {
        let (hist, initial_duration, overflow) = handle.join().unwrap();

        if print_statistics {
            if initial_duration > histogram_upper_bound {
                upper_bound_overflow = initial_duration;
            } else {
                overall_initial_hist.record(initial_duration as u64).unwrap();
            }

            overall_hist.add(hist).expect("histogram.add failed");
            if overflow > upper_bound_overflow {
                upper_bound_overflow = overflow;
            }
        }
    }

    if print_statistics {
        println!("");
        println!("# of devices - {}", device_number);
        _print_statistics("initial histogram", overall_initial_hist);
        println!("");
        println!("# of non-initial attempts - {}", (attempts_count - 1) * device_number);
        _print_statistics("performance histogram", overall_hist);
        println!("");
        if upper_bound_overflow > 0 {
            println!("Histogram overflow {}", upper_bound_overflow);
        }
    }

    println!("Updates checking completed");
    Ok(())
}

fn _worker(device_number: u32, config_path: &CStr, checking_start_time: Instant,
    attempts_count: u32, calculate_statistics: bool, upper_bound: u32)
    -> (Histogram::<u32>, u32, u32)
{
    let aktualizr_handle = unsafe { Aktualizr_create_from_path(config_path.as_ptr()) };
    let init_result = unsafe { Aktualizr_initialize(aktualizr_handle) };
    println!("device_number {:3}, initialization result = {}", device_number, init_result);

    let mut upper_bound_overflow = 0;
    let mut initial_duration = 0;
    let mut hist =
        Histogram::<u32>::new_with_bounds(1, upper_bound as u64, 2).expect("Histogram creation failed");

    for _attempt in 0..attempts_count
    {
        let start = Instant::now();
        let updates = unsafe { Aktualizr_updates_check(aktualizr_handle) };
        let duration = start.elapsed();
        let duration_u32 = duration.as_millis() as u32;
        println!("device_number {:3}, attempt {:2}, duration = {:5?} ms",
            device_number, _attempt, duration_u32);

        if calculate_statistics
        {
            if _attempt == 0 {
                initial_duration = duration_u32;
            } else {
                if duration_u32 > upper_bound {
                    upper_bound_overflow = duration_u32;
                } else {
                    hist.record(duration_u32 as u64).unwrap();
                }
            }
        }

        unsafe { Aktualizr_updates_free(updates); }

        if attempts_count == 1 {
            break;
        }

        let time_between_updates_check_calls = 2000 /*ms*/;
        let mut end_of_sleep = time_between_updates_check_calls * (_attempt + 1);
        let time_from_checking_start = checking_start_time.elapsed().as_millis() as u32;

        if time_from_checking_start > end_of_sleep {
            //Aktualizr_updates_check call was too slow
            let delay = time_from_checking_start - end_of_sleep;
            end_of_sleep = end_of_sleep + time_between_updates_check_calls *
                ( 1 + delay / time_between_updates_check_calls );
        }
        let remained_pause = end_of_sleep - time_from_checking_start;
        thread::sleep(time::Duration::from_millis(remained_pause as u64));
    }

    unsafe { Aktualizr_destroy(aktualizr_handle) };

    return (hist, initial_duration, upper_bound_overflow);
}

fn _print_statistics(histogram_name: &str, histogram: Histogram::<u32>)
{
    println!("# of {} samples: {}", histogram_name, histogram.len());
    for percentile in (0 .. 110).step_by(10) {
        println!("{:3?}'th percentile of {}: {:5} ms",
            percentile, histogram_name,
            histogram.value_at_quantile(percentile as f64 * 0.01));
    }
}
