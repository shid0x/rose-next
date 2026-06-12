use std::ffi;
use std::path::Path;

use libc::*;
use log::{logger, Level, RecordBuilder};

use crate::logger::CoreLogger;

#[repr(C)]
#[derive(PartialEq)]
#[allow(unused)]
pub enum LogLevel {
    Trace,
    Debug,
    Info,
    Warn,
    Error,
    Off,
}

impl LogLevel {
    pub fn to_level(&self) -> Level {
        match self {
            LogLevel::Trace => Level::Trace,
            LogLevel::Debug => Level::Debug,
            LogLevel::Info => Level::Info,
            LogLevel::Warn => Level::Warn,
            LogLevel::Error => Level::Error,
            LogLevel::Off => Level::Trace,
        }
    }
}

#[no_mangle]
pub extern "C" fn logger_init(path: *const c_char, level: LogLevel) {
    let path = unsafe {
        ffi::CStr::from_ptr(path as *mut c_char)
            .to_str()
            .unwrap_or("log/rose-next.log")
    };

    CoreLogger::init(Path::new(path));

    let level = match level {
        LogLevel::Trace => log::LevelFilter::Trace,
        LogLevel::Debug => log::LevelFilter::Debug,
        LogLevel::Info => log::LevelFilter::Info,
        LogLevel::Warn => log::LevelFilter::Warn,
        LogLevel::Error => log::LevelFilter::Error,
        LogLevel::Off => log::LevelFilter::Off,
    };

    log::set_max_level(level);
}

#[no_mangle]
pub extern "C" fn logger_write(
    level: LogLevel,
    file: *const c_char,
    line: u32,
    msg: *const c_char,
) {
    let msg_string = unsafe {
        ffi::CStr::from_ptr(msg as *mut c_char)
            .to_str()
            .unwrap_or("")
    };

    let file_string = unsafe {
        ffi::CStr::from_ptr(file as *mut c_char)
            .to_str()
            .unwrap_or("")
    };

    let file_path = Path::new(file_string);

    if level == LogLevel::Off || msg_string.is_empty() {
        return;
    }

    let mut record_builder = RecordBuilder::new();
    logger().log(
        &record_builder
            .args(format_args!("{}", msg_string))
            .level(level.to_level())
            .target("")
            .file(Some(
                file_path
                    .file_name()
                    .unwrap_or_default()
                    .to_str()
                    .unwrap_or_default(),
            ))
            .line(Some(line))
            .module_path(None)
            .build(),
    );
}
