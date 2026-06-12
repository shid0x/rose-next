use std::ffi::CStr;
use std::fs::File;
use std::io::Read;

use toml;
use toml::Value as Toml;

use crate::ffi::string::FfiString;

#[no_mangle]
pub unsafe extern "C" fn toml_load(path: *const libc::c_char) -> *mut Toml {
    let path = CStr::from_ptr(path).to_str().unwrap_or_default();

    let mut toml_string = String::new();
    let res = File::open(&path)
        .and_then(|mut f| f.read_to_string(&mut toml_string))
        .is_ok();

    if !res {
        return std::ptr::null_mut();
    }

    let decoded = toml::from_str(&toml_string).unwrap_or(Toml::String(String::from("")));
    Box::into_raw(Box::new(decoded))
}

#[no_mangle]
pub unsafe extern "C" fn toml_free(t: *mut Toml) {
    Box::from_raw(t);
}

#[no_mangle]
pub unsafe extern "C" fn toml_get_int(
    t: *mut Toml,
    table: *const libc::c_char,
    key: *const libc::c_char,
    out: *mut i64,
) -> bool {
    let t = Box::from_raw(t);
    let table = CStr::from_ptr(table).to_str().unwrap_or_default();
    let key = CStr::from_ptr(key).to_str().unwrap_or_default();

    // Get root table
    let mut val = t.as_table();

    // Get sub table
    if !table.is_empty() {
        val = val.and_then(|v| v.get(table)).and_then(|v| v.as_table());
    }

    // Get value
    let val = val.and_then(|v| v.get(key)).and_then(|v| v.as_integer());
    let res = if let Some(v) = val {
        *out = v;
        true
    } else {
        false
    };

    std::mem::forget(t);
    res
}

#[no_mangle]
pub unsafe extern "C" fn toml_get_str(
    t: *mut Toml,
    table: *const libc::c_char,
    key: *const libc::c_char,
    mut _out: *mut FfiString,
) -> bool {
    let t = Box::from_raw(t);
    let table = CStr::from_ptr(table).to_str().unwrap_or_default();
    let key = CStr::from_ptr(key).to_str().unwrap_or_default();

    // Get root table
    let mut val = t.as_table();

    // Get sub table
    if !table.is_empty() {
        val = val.and_then(|v| v.get(table)).and_then(|v| v.as_table());
    }

    // Get value
    let val = val.and_then(|v| v.get(key)).and_then(|v| v.as_str());
    let res = if let Some(v) = val {
        let mut ffi_str = Box::from_raw(_out);
        ffi_str.set_string(&v);
        std::mem::forget(ffi_str);
        true
    } else {
        false
    };

    std::mem::forget(t);
    res
}
