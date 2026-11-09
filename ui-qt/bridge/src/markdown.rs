//! Presentation ranges in the original source. Never serialize a Markdown AST back to text.
use pulldown_cmark::{CodeBlockKind, Event, Options, Parser, Tag, TagEnd};
use serde_json::{json, Value};

pub fn spans(source: &str) -> Vec<Value> {
    let mut offsets = vec![0; source.len() + 1];
    let mut utf16 = 0;
    for (at, ch) in source.char_indices() {
        for offset in &mut offsets[at..at + ch.len_utf8()] { *offset = utf16; }
        utf16 += ch.len_utf16();
    }
    offsets[source.len()] = utf16;
    let mut result = Vec::new();
    let options = Options::ENABLE_TABLES | Options::ENABLE_STRIKETHROUGH
        | Options::ENABLE_TASKLISTS | Options::ENABLE_FOOTNOTES | Options::ENABLE_MATH
        | Options::ENABLE_YAML_STYLE_METADATA_BLOCKS | Options::ENABLE_GFM;
    let mut opaque_depth = 0usize;
    for (event, range) in Parser::new_ext(source, options).into_offset_iter() {
        match &event {
            Event::Start(Tag::CodeBlock(_) | Tag::HtmlBlock | Tag::MetadataBlock(_)) => opaque_depth += 1,
            Event::End(TagEnd::CodeBlock | TagEnd::HtmlBlock | TagEnd::MetadataBlock(_)) => opaque_depth = opaque_depth.saturating_sub(1),
            _ => {}
        }
        let raw = &source[range.clone()];
        let mut kind = "";
        let mut value = String::new();
        let mut level = 0;
        let mut marks = Vec::new();
        let mut mark = |a: usize, b: usize| {
            if a < b && b <= source.len() { marks.push(json!([offsets[a], offsets[b]])); }
        };
        match event {
            Event::Start(tag) => match tag {
                Tag::Emphasis | Tag::Strong | Tag::Strikethrough => {
                    let count = if matches!(tag, Tag::Emphasis) { 1 } else { 2 };
                    kind = match tag { Tag::Emphasis => "italic", Tag::Strong => "bold", _ => "strike" };
                    mark(range.start, range.start + count);
                    mark(range.end - count, range.end);
                }
                Tag::Heading { level: l, .. } => {
                    kind = "heading"; level = l as usize;
                    let prefix = raw.bytes().take_while(|b| *b == b'#' || *b == b' ').count();
                    if raw.trim_start().starts_with('#') {
                        mark(range.start, range.start + prefix);
                        let trimmed = raw.trim_end();
                        let hashes = trimmed.bytes().rev().take_while(|b| *b == b'#').count();
                        if hashes > 0 && trimmed.len() > hashes && trimmed.as_bytes()[trimmed.len()-hashes-1].is_ascii_whitespace() {
                            mark(range.start+trimmed.len()-hashes-1, range.start+trimmed.len());
                        }
                    } else if let Some(at) = raw.trim_end().rfind('\n') {
                        mark(range.start + at + 1, range.end - usize::from(raw.ends_with('\n')));
                    }
                }
                Tag::CodeBlock(code) => {
                    kind = "codeblock";
                    if let CodeBlockKind::Fenced(info) = code {
                        value = info.to_string();
                        let first_end = raw.find('\n').unwrap_or(raw.len());
                        mark(range.start, range.start + first_end);
                        let trimmed = raw.trim_end_matches(['\r', '\n']);
                        if let Some(last) = trimmed.rfind('\n') {
                            let closer = trimmed[last+1..].trim();
                            let opener = raw.trim_start().chars().next().unwrap_or('`');
                            if closer.len() >= 3 && closer.chars().all(|c| c == opener) {
                                mark(range.start+last+1, range.start+trimmed.len());
                            }
                        }
                    }
                }
                Tag::HtmlBlock => kind = "html",
                Tag::MetadataBlock(_) => kind = "metadata",
                Tag::BlockQuote(_) => {
                    kind = "quote";
                    let mut at = range.start;
                    for line in raw.split_inclusive('\n') {
                        let spaces = line.bytes().take_while(|b| *b == b' ').count();
                        if line[spaces..].starts_with('>') { mark(at+spaces, at+spaces+1); }
                        at += line.len();
                    }
                }
                Tag::Item => {
                    let s = raw.bytes().take_while(|b| *b == b' ').count();
                    if matches!(raw.as_bytes().get(s), Some(b'-' | b'+' | b'*')) {
                        kind = "bullet"; mark(range.start+s, range.start+s+1);
                    }
                }
                Tag::Link { dest_url, .. } | Tag::Image { dest_url, .. } => {
                    kind = if raw.starts_with("![") { "image" } else { "link" };
                    value = dest_url.to_string();
                    let opening = if kind == "image" { 2 } else { 1 };
                    if raw.starts_with('[') || raw.starts_with("![") {
                        let mut depth = 1;
                        let mut escaped = false;
                        for (at, ch) in raw.char_indices().skip_while(|(i,_)| *i < opening) {
                            if escaped { escaped = false; continue; }
                            if ch == '\\' { escaped = true; continue; }
                            if ch == '[' { depth += 1; }
                            if ch == ']' { depth -= 1; }
                            if depth == 0 {
                                if at > opening { mark(range.start,range.start+opening); mark(range.start+at,range.end); }
                                break;
                            }
                        }
                    }
                }
                _ => {}
            },
            Event::Code(_) => {
                kind = "code";
                let count = raw.bytes().take_while(|b| *b == b'`').count();
                mark(range.start, range.start+count); mark(range.end-count, range.end);
            }
            Event::InlineMath(equation) | Event::DisplayMath(equation) => {
                let display = raw.starts_with("$$");
                let valid = display || (!equation.starts_with(char::is_whitespace)
                    && !equation.ends_with(char::is_whitespace)
                    && !source[range.end..].starts_with(|c:char| c.is_ascii_digit()));
                // A blank line terminates a display construct, as in Zettlr.
                if valid && !equation.contains("\n\n") {
                    kind = if display { "displaymath" } else { "math" };
                    value = equation.to_string();
                }
            }
            Event::InlineHtml(_) => kind = "html",
            Event::TaskListMarker(checked) => { kind = "task"; value = checked.to_string(); }
            Event::FootnoteReference(_) => kind = "footnote",
            Event::Rule => kind = "rule",
            Event::SoftBreak => {
                kind = "softbreak";
                // The rendered newline supplies one space. Suppress source
                // padding around it, including continuation-line indentation.
                let mut before = range.start;
                while before > 0 && matches!(source.as_bytes()[before-1], b' ' | b'\t') { before -= 1; }
                let mut after = range.end;
                while after < source.len() && matches!(source.as_bytes()[after], b' ' | b'\t') { after += 1; }
                mark(before, range.start);
                mark(range.end, after);
            }
            Event::Text(_) if opaque_depth == 0 && raw.starts_with('\\') && raw.len() > 1 => {
                kind = "escape"; mark(range.start, range.start+1);
            }
            _ => {}
        }
        if !kind.is_empty() {
            result.push(json!({"kind":kind,"from":offsets[range.start],"to":offsets[range.end],
                "marks":marks,"value":value,"level":level}));
        }
    }
    result
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn syntax_ranges_are_source_offsets() {
        let text = "é😀 **bold** and ``a`b`` then $x_2$.\n";
        let spans = spans(text);
        let code = spans.iter().find(|s| s["kind"]=="code").unwrap();
        let a=code["from"].as_u64().unwrap() as usize;
        let b=code["to"].as_u64().unwrap() as usize;
        assert_eq!(String::from_utf16(&text.encode_utf16().collect::<Vec<_>>()[a..b]).unwrap(),"``a`b``");
        assert!(spans.iter().any(|s| s["kind"]=="math"));
    }
    #[test]
    fn html_and_code_are_opaque() {
        let nodes = spans("<div>**literal** $x$</div>\n\n```tex\n$x$ **literal**\n```\n");
        assert!(nodes.iter().any(|s| s["kind"]=="html"));
        assert!(nodes.iter().any(|s| s["kind"]=="codeblock"));
        assert!(!nodes.iter().any(|s| s["kind"]=="math" || s["kind"]=="bold"));
    }
    #[test]
    fn typing_delimiters_changes_classification() {
        assert!(!spans("`blah").iter().any(|s| s["kind"]=="code"));
        assert!(spans("`blah`").iter().any(|s| s["kind"]=="code"));
        assert!(!spans("$ 20 and $30").iter().any(|s| s["kind"]=="math"));
    }
    #[test]
    fn incomplete_math_and_code_keep_their_source_ranges() {
        assert!(!spans("$$\nunclosed").iter().any(|s| s["kind"]=="displaymath"));
        let code=spans("```tex\n\\alpha + **x**\n```\n");
        assert!(!code.iter().any(|s| s["kind"]=="escape" || s["kind"]=="bold"));
        let fence=code.iter().find(|s| s["kind"]=="codeblock").unwrap();
        assert_eq!(fence["marks"].as_array().unwrap().len(),2);
        assert!(!spans("<iframe src=\"https://example.org\"></iframe>\n")
            .iter().any(|s| s["kind"]=="link" || s["kind"]=="image"));
    }
    #[test]
    fn soft_breaks_exclude_structural_and_literal_newlines() {
        let source = "é😀 first\nsecond\n\nhard  \nnext\\\nlast\n\n```\ncode\nmore\n```\n\n<div>\nraw\n</div>\n\n- a\n- b\n";
        let nodes = spans(source);
        let breaks: Vec<_> = nodes.iter().filter(|s| s["kind"] == "softbreak").collect();
        assert_eq!(breaks.len(), 1);
        assert_eq!(breaks[0]["from"], source[..source.find('\n').unwrap()].encode_utf16().count());
        assert_eq!(breaks[0]["to"].as_u64().unwrap(), breaks[0]["from"].as_u64().unwrap()+1);
    }
    #[test]
    fn soft_break_padding_is_presentation_only() {
        let source = "- one\n  two\n\nthree \n\tfour\n\ncode ` a  b `\n\nhard  \n  break\n";
        let nodes = spans(source);
        let breaks: Vec<_> = nodes.iter().filter(|s| s["kind"] == "softbreak").collect();
        assert_eq!(breaks.len(), 2);
        assert_eq!(breaks[0]["marks"], json!([[6,8]]));
        let at = source.find(" \n\t").unwrap();
        assert_eq!(breaks[1]["marks"], json!([[at,at+1],[at+2,at+3]]));
    }
}
