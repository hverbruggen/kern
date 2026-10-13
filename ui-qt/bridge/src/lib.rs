#[allow(dead_code)] // the app uses only part of the shared table API
#[path = "../../../core/src/table.rs"]
mod table;

use std::ffi::{c_char, CStr, CString};

fn from_c(pointer: *const c_char) -> Option<String> {
    if pointer.is_null() {
        return None;
    }
    unsafe { CStr::from_ptr(pointer) }
        .to_str()
        .ok()
        .map(str::to_owned)
}

fn into_c(value: String) -> *mut c_char {
    CString::new(value).map_or(std::ptr::null_mut(), CString::into_raw)
}

#[no_mangle]
pub extern "C" fn kern_free_string(pointer: *mut c_char) {
    if !pointer.is_null() {
        unsafe { drop(CString::from_raw(pointer)) };
    }
}

#[no_mangle]
pub extern "C" fn kern_tables_json(source: *const c_char) -> *mut c_char {
    let Some(source) = from_c(source) else {
        return std::ptr::null_mut();
    };
    let tables: Vec<_> = table::find_tables(&source)
        .into_iter()
        .map(|t| {
            let align: Vec<_> = t
                .aligns
                .iter()
                .map(|a| match a {
                    table::Align::None => "none",
                    table::Align::Left => "left",
                    table::Align::Center => "center",
                    table::Align::Right => "right",
                })
                .collect();
            serde_json::json!({
                "start": t.byte_range.0,
                "end": t.byte_range.1,
                "header": t.header,
                "rows": t.rows,
                "align": align,
            })
        })
        .collect();
    into_c(serde_json::to_string(&tables).unwrap_or_default())
}

#[no_mangle]
pub extern "C" fn kern_replace_cell(
    source: *const c_char,
    table_index: usize,
    row: usize,
    col: usize,
    value: *const c_char,
) -> *mut c_char {
    let (Some(source), Some(value)) = (from_c(source), from_c(value)) else {
        return std::ptr::null_mut();
    };
    table::replace_cell_source(&source, table_index, row, col, &value)
        .map(into_c)
        .unwrap_or(std::ptr::null_mut())
}

#[no_mangle]
pub extern "C" fn kern_table_command(
    source: *const c_char,
    table_index: usize,
    command: *const c_char,
    row: usize,
    col: usize,
) -> *mut c_char {
    let (Some(source), Some(command)) = (from_c(source), from_c(command)) else {
        return std::ptr::null_mut();
    };
    let Some(mut t) = table::find_tables(&source).into_iter().nth(table_index) else {
        return std::ptr::null_mut();
    };
    match command.as_str() {
        "row-above" => t.insert_row(row.saturating_sub(1)),
        "row-below" => t.insert_row(row),
        "col-before" => t.insert_col(col),
        "col-after" => t.insert_col(col + 1),
        "row-delete" if row > 0 && row <= t.rows.len() => {
            t.rows.remove(row - 1);
        }
        "col-delete" if t.col_count() > 1 && col < t.col_count() => {
            t.header.remove(col);
            t.aligns.remove(col);
            for body in &mut t.rows {
                body.remove(col);
            }
        }
        _ => return std::ptr::null_mut(),
    }
    let mut result = String::with_capacity(source.len());
    result.push_str(&source[..t.byte_range.0]);
    result.push_str(&t.render());
    result.push_str(&source[t.byte_range.1..]);
    into_c(result)
}

#[no_mangle]
pub extern "C" fn kern_new_table(rows: usize, cols: usize) -> *mut c_char {
    into_c(table::new_table(rows, cols))
}
