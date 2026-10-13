use kern_core::table::{find_tables, new_table, replace_cell_source, Align, PipeTable};

const SAMPLE: &str = "\
intro paragraph

| Name | Age |
| --- | ---: |
| Alice | 30 |
| Bob | 25 |

after
";

#[test]
fn finds_one_table_with_correct_shape() {
    let tables = find_tables(SAMPLE);
    assert_eq!(tables.len(), 1);
    let t = &tables[0];
    assert_eq!(t.header, vec!["Name", "Age"]);
    assert_eq!(t.aligns, vec![Align::None, Align::Right]);
    assert_eq!(t.rows.len(), 2);
    assert_eq!(t.rows[1], vec!["Bob", "25"]);
    assert_eq!(t.col_count(), 2);
    assert_eq!(t.row_count(), 3);
}

#[test]
fn byte_range_covers_exactly_the_table() {
    let tables = find_tables(SAMPLE);
    let (s, e) = tables[0].byte_range;
    let slice = &SAMPLE[s..e];
    assert!(slice.starts_with("| Name | Age |"));
    assert!(slice.ends_with("| Bob | 25 |"));
    assert!(!slice.ends_with('\n'));
}

#[test]
fn render_round_trips_alignment() {
    let tables = find_tables(SAMPLE);
    let rendered = tables[0].render();
    // Re-parsing the rendered text yields an equivalent table (modulo range).
    let reparsed = find_tables(&format!("{rendered}\n"));
    assert_eq!(reparsed.len(), 1);
    assert_eq!(reparsed[0].header, tables[0].header);
    assert_eq!(reparsed[0].aligns, tables[0].aligns);
    assert_eq!(reparsed[0].rows, tables[0].rows);
}

#[test]
fn render_emits_alignment_markers() {
    let t = find_tables(SAMPLE).remove(0);
    let rendered = t.render();
    assert!(rendered.contains("| --- | ---: |"), "got:\n{rendered}");
}

#[test]
fn insert_row_adds_empty_body_row() {
    let mut t = find_tables(SAMPLE).remove(0);
    t.insert_row(1);
    assert_eq!(t.rows.len(), 3);
    assert_eq!(t.rows[1], vec![String::new(), String::new()]);
    // Order preserved around the insertion point.
    assert_eq!(t.rows[0], vec!["Alice", "30"]);
    assert_eq!(t.rows[2], vec!["Bob", "25"]);
}

#[test]
fn insert_row_clamps_out_of_range() {
    let mut t = find_tables(SAMPLE).remove(0);
    t.insert_row(999);
    assert_eq!(t.rows.len(), 3);
    assert_eq!(t.rows[2], vec![String::new(), String::new()]);
}

#[test]
fn insert_col_extends_header_aligns_and_rows() {
    let mut t = find_tables(SAMPLE).remove(0);
    t.insert_col(1);
    assert_eq!(t.header, vec!["Name", "", "Age"]);
    assert_eq!(t.aligns, vec![Align::None, Align::None, Align::Right]);
    assert_eq!(t.rows[0], vec!["Alice", "", "30"]);
    assert_eq!(t.col_count(), 3);
}

#[test]
fn set_cell_addresses_header_and_body() {
    let mut t = find_tables(SAMPLE).remove(0);
    t.set_cell(0, 0, "Person");
    t.set_cell(2, 1, "26");
    assert_eq!(t.header[0], "Person");
    assert_eq!(t.rows[1][1], "26");
}

#[test]
fn new_table_has_header_plus_body_rows() {
    let src = new_table(3, 2);
    let t = find_tables(&format!("{src}\n")).remove(0);
    assert_eq!(t.col_count(), 2);
    assert_eq!(t.row_count(), 3); // header + 2 body
    assert_eq!(t.rows.len(), 2);
    assert!(t.header.iter().chain(t.rows.iter().flatten()).all(String::is_empty));
    assert_eq!(src.lines().next(), Some("|  |  |"));
}

#[test]
fn ragged_rows_are_normalized_to_col_count() {
    let src = "| a | b | c |\n| --- | --- | --- |\n| 1 | 2 |\n| 1 | 2 | 3 | 4 |\n";
    let t = find_tables(src).remove(0);
    assert_eq!(t.rows[0], vec!["1", "2", ""]);
    assert_eq!(t.rows[1], vec!["1", "2", "3"]);
}

#[test]
fn plain_text_has_no_tables() {
    assert!(find_tables("just words\nand more\n").is_empty());
    // A pipe line without a delimiter row is not a table.
    assert!(find_tables("| not | a table |\njust text\n").is_empty());
}

#[test]
fn manual_construction_renders() {
    let t = PipeTable {
        header: vec!["x".into(), "y".into()],
        aligns: vec![Align::Center, Align::Left],
        rows: vec![vec!["1".into(), "2".into()]],
        byte_range: (0, 0),
    };
    assert_eq!(t.render(), "| x | y |\n| :---: | :--- |\n| 1 | 2 |");
}

#[test]
fn table_scanner_ignores_fences_and_keeps_escaped_pipes() {
    let src = "```md\n| fake | table |\n| --- | --- |\n```\n\n| A | B |\n| --- | --- |\n| `x\\|y` | a\\|b |\n";
    let tables = find_tables(src);
    assert_eq!(tables.len(), 1);
    assert_eq!(tables[0].rows[0], vec!["`x\\|y`", "a\\|b"]);
}

#[test]
fn unbalanced_backtick_does_not_swallow_later_cells() {
    let src = "| a | b | c |\n| --- | --- | --- |\n| 1 | `x | 3 |\n| 4 | 5 | 6 |\n";
    let tables = find_tables(src);
    assert_eq!(tables[0].rows, vec![vec!["1", "`x", "3"], vec!["4", "5", "6"]]);
    let edited = replace_cell_source("| a | b |\n| --- | --- |\n| x | y |\n", 0, 1, 0, "it`s").unwrap();
    assert_eq!(find_tables(&edited)[0].rows, vec![vec!["it`s", "y"]]);
}

#[test]
fn unescaped_pipe_inside_code_splits_cells_like_github() {
    let src = "| a | b |\n| --- | --- |\n| `x|y` | z |\n";
    assert_eq!(find_tables(src)[0].rows[0], vec!["`x", "y`"]);
}

#[test]
fn cell_edit_preserves_all_other_source_bytes() {
    let src = "# **Title**\r\n\r\n| A   | B |\r\n| :--- | --:|\r\n| one | a\\|b |\r\n\r\nEnd  \r\n";
    let edited = replace_cell_source(src, 0, 1, 0, "new|value").unwrap();
    assert_eq!(
        edited,
        "# **Title**\r\n\r\n| A   | B |\r\n| :--- | --:|\r\n| new\\|value | a\\|b |\r\n\r\nEnd  \r\n"
    );
}

#[test]
fn cell_edit_does_not_double_escape_existing_pipe() {
    let src = "| A | B |\n| --- | --- |\n| old | two |\n";
    let edited = replace_cell_source(src, 0, 1, 0, "a\\|b").unwrap();
    assert!(edited.contains("| a\\|b | two |"));
}
