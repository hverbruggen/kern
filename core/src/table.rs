//! Pure, UI-agnostic Markdown pipe-table logic: detection, canonical
//! rendering, and structural edits (insert row/column, new table).
//!
//! The Qt Quick frontend renders each detected table as a grid and hides the
//! underlying source; these functions are the single source of truth for the
//! `| a | b |` text that lives in the buffer and gets saved.

/// Column alignment, as encoded by the delimiter row (`:---`, `:---:`, `---:`).
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Align {
    None,
    Left,
    Center,
    Right,
}

impl Align {
    /// The delimiter-cell text for this alignment (without surrounding spaces).
    fn delimiter(self) -> &'static str {
        match self {
            Align::None => "---",
            Align::Left => ":---",
            Align::Center => ":---:",
            Align::Right => "---:",
        }
    }

    fn parse(cell: &str) -> Align {
        let t = cell.trim();
        let left = t.starts_with(':');
        let right = t.ends_with(':');
        match (left, right) {
            (true, true) => Align::Center,
            (true, false) => Align::Left,
            (false, true) => Align::Right,
            (false, false) => Align::None,
        }
    }
}

/// A parsed GitHub-flavored Markdown pipe table.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct PipeTable {
    /// Header cells (row 0 in the rendered grid).
    pub header: Vec<String>,
    /// Per-column alignment; same length as `header`.
    pub aligns: Vec<Align>,
    /// Body rows (below the delimiter). Each is normalized to `col_count`.
    pub rows: Vec<Vec<String>>,
    /// Byte range of the whole table in the source, excluding the trailing
    /// newline after the last row: `[start, end)`.
    pub byte_range: (usize, usize),
}

impl PipeTable {
    pub fn col_count(&self) -> usize {
        self.header.len()
    }

    /// Total rows including the header row.
    pub fn row_count(&self) -> usize {
        1 + self.rows.len()
    }

    /// Reconstructs canonical Markdown source for the table (no trailing
    /// newline). Cells are padded with a single surrounding space.
    pub fn render(&self) -> String {
        let cols = self.col_count().max(1);
        let mut out = String::new();

        render_row(&mut out, &self.header, cols);
        out.push('\n');

        out.push('|');
        for c in 0..cols {
            let a = self.aligns.get(c).copied().unwrap_or(Align::None);
            out.push(' ');
            out.push_str(a.delimiter());
            out.push_str(" |");
        }

        for row in &self.rows {
            out.push('\n');
            render_row(&mut out, row, cols);
        }
        out
    }

    /// Inserts an empty body row at `body_index` (clamped to `0..=rows.len()`).
    pub fn insert_row(&mut self, body_index: usize) {
        let idx = body_index.min(self.rows.len());
        self.rows.insert(idx, vec![String::new(); self.col_count()]);
    }

    /// Inserts an empty column at `col_index` in the header, alignments, and
    /// every body row (clamped to `0..=col_count`).
    pub fn insert_col(&mut self, col_index: usize) {
        let idx = col_index.min(self.col_count());
        self.header.insert(idx, String::new());
        self.aligns.insert(idx, Align::None);
        for row in &mut self.rows {
            let at = idx.min(row.len());
            row.insert(at, String::new());
        }
    }

    /// Sets the text of a cell addressed as `ui_row` (0 = header, 1.. = body)
    /// and `col`. Out-of-range addresses are ignored.
    pub fn set_cell(&mut self, ui_row: usize, col: usize, text: &str) {
        let target = if ui_row == 0 {
            self.header.get_mut(col)
        } else {
            self.rows.get_mut(ui_row - 1).and_then(|r| r.get_mut(col))
        };
        if let Some(cell) = target {
            *cell = text.to_string();
        }
    }
}

fn render_row(out: &mut String, cells: &[String], cols: usize) {
    out.push('|');
    for c in 0..cols {
        let cell = cells.get(c).map(String::as_str).unwrap_or("");
        out.push(' ');
        out.push_str(cell.trim());
        out.push_str(" |");
    }
}

/// A blank table with `rows` total rows (1 header + `rows-1` body) and `cols`
/// columns. Every cell is empty so typing starts in a clean table.
pub fn new_table(rows: usize, cols: usize) -> String {
    let cols = cols.max(1);
    let body_rows = rows.max(1).saturating_sub(1);
    let table = PipeTable {
        header: vec![String::new(); cols],
        aligns: vec![Align::None; cols],
        rows: vec![vec![String::new(); cols]; body_rows],
        byte_range: (0, 0),
    };
    table.render()
}

/// Finds all pipe tables in `text`, in source order.
pub fn find_tables(text: &str) -> Vec<PipeTable> {
    let lines: Vec<(usize, &str)> = text
        .split_inclusive('\n')
        .scan(0usize, |offset, line| {
            let start = *offset;
            *offset += line.len();
            Some((start, line.trim_end_matches('\n').trim_end_matches('\r')))
        })
        .collect();

    let mut tables = Vec::new();
    let mut i = 0;
    let mut fence: Option<(u8, usize)> = None;
    while i + 1 < lines.len() {
        let header = lines[i].1;
        if let Some((marker, count)) = fence {
            if fence_marker(header).is_some_and(|(m, n)| m == marker && n >= count) {
                fence = None;
            }
            i += 1;
            continue;
        }
        if let Some(open) = fence_marker(header) {
            fence = Some(open);
            i += 1;
            continue;
        }
        let delimiter = lines[i + 1].1;
        if header.starts_with("    ")
            || !looks_like_table_row(header)
            || !looks_like_delimiter_row(delimiter)
            || split_row(header).len() != split_row(delimiter).len()
        {
            i += 1;
            continue;
        }

        let header_cells = split_row(header);
        let col_count = header_cells.len();
        let aligns: Vec<Align> = {
            let cells = split_row(delimiter);
            (0..col_count)
                .map(|c| cells.get(c).map(|s| Align::parse(s)).unwrap_or(Align::None))
                .collect()
        };

        let start = lines[i].0;
        let mut end = lines[i + 1].0 + lines[i + 1].1.len();
        let mut rows = Vec::new();
        i += 2;
        while i < lines.len() && looks_like_table_row(lines[i].1) {
            let mut cells = split_row(lines[i].1);
            cells.resize(col_count, String::new());
            cells.truncate(col_count);
            rows.push(cells);
            end = lines[i].0 + lines[i].1.len();
            i += 1;
        }

        tables.push(PipeTable {
            header: header_cells,
            aligns,
            rows,
            byte_range: (start, end),
        });
    }
    tables
}

fn looks_like_table_row(line: &str) -> bool {
    let trimmed = line.trim();
    trimmed.contains('|') && split_row(trimmed).len() >= 2
}

fn looks_like_delimiter_row(line: &str) -> bool {
    let cells = split_row(line);
    cells.len() >= 2
        && cells.iter().all(|cell| {
            let trimmed = cell.trim();
            trimmed.len() >= 3
                && trimmed.chars().all(|c| matches!(c, '-' | ':' | ' '))
                && trimmed.chars().any(|c| c == '-')
        })
}

fn split_row(line: &str) -> Vec<String> {
    split_row_ranges(line)
        .into_iter()
        .map(|(start, end)| line[start..end].to_string())
        .collect()
}

/// Byte ranges for the contents of cells within a single source line.
fn split_row_ranges(line: &str) -> Vec<(usize, usize)> {
    let trimmed = line.trim();
    let base = line.len() - line.trim_start().len();
    let bytes = trimmed.as_bytes();
    let mut separators = Vec::new();
    let mut slash_count = 0usize;
    for (i, &byte) in bytes.iter().enumerate() {
        match byte {
            b'\\' => slash_count += 1,
            // Like GitHub, a pipe splits cells unless a backslash escapes it, even inside code.
            b'|' if slash_count % 2 == 0 => {
                separators.push(i);
                slash_count = 0;
            }
            _ => slash_count = 0,
        }
    }
    if separators.is_empty() {
        return vec![trim_cell_range(line, base, base + trimmed.len())];
    }
    let start = if separators.first() == Some(&0) {
        separators.remove(0);
        1
    } else {
        0
    };
    let end = if separators.last() == Some(&trimmed.len().saturating_sub(1)) {
        separators.pop();
        trimmed.len().saturating_sub(1)
    } else {
        trimmed.len()
    };
    let mut ranges = Vec::new();
    let mut from = start;
    for at in separators {
        ranges.push(trim_cell_range(line, base + from, base + at));
        from = at + 1;
    }
    ranges.push(trim_cell_range(line, base + from, base + end));
    ranges
}

fn trim_cell_range(line: &str, start: usize, end: usize) -> (usize, usize) {
    let cell = &line[start..end];
    let left = cell.len() - cell.trim_start().len();
    let right = cell.trim_end().len();
    (start + left, start + right.max(left))
}

fn fence_marker(line: &str) -> Option<(u8, usize)> {
    let indent = line.len() - line.trim_start_matches(' ').len();
    if indent > 3 {
        return None;
    }
    let bytes = &line.as_bytes()[indent..];
    let marker = *bytes.first()?;
    if marker != b'`' && marker != b'~' {
        return None;
    }
    let count = bytes.iter().take_while(|&&c| c == marker).count();
    (count >= 3).then_some((marker, count))
}

/// Replaces one displayed cell while preserving all bytes outside its content.
/// `ui_row` is zero for the header and one or more for body rows.
pub fn replace_cell_source(
    text: &str,
    table_index: usize,
    ui_row: usize,
    col: usize,
    replacement: &str,
) -> Option<String> {
    if replacement.contains(['\n', '\r']) {
        return None;
    }
    let table = find_tables(text).into_iter().nth(table_index)?;
    if col >= table.col_count() || ui_row >= table.row_count() {
        return None;
    }
    let table_text = &text[table.byte_range.0..table.byte_range.1];
    let source_line = table_text.lines().nth(ui_row + usize::from(ui_row > 0))?;
    let line_offset: usize = table_text
        .split_inclusive('\n')
        .take(ui_row + usize::from(ui_row > 0))
        .map(str::len)
        .sum();
    let (start, end) = *split_row_ranges(source_line).get(col)?;
    let absolute = table.byte_range.0 + line_offset;
    let escaped = escape_cell_pipes(replacement);
    let mut result = String::with_capacity(text.len() + escaped.len());
    result.push_str(&text[..absolute + start]);
    result.push_str(&escaped);
    result.push_str(&text[absolute + end..]);
    Some(result)
}

fn escape_cell_pipes(value: &str) -> String {
    let mut result = String::with_capacity(value.len());
    let mut backslashes = 0usize;
    for ch in value.chars() {
        if ch == '|' && backslashes % 2 == 0 {
            result.push('\\');
        }
        result.push(ch);
        backslashes = if ch == '\\' { backslashes + 1 } else { 0 };
    }
    result
}
