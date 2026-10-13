#pragma once

#include <QRegularExpression>
#include <QString>
#include <QStringList>

// Qt's Markdown exporter drops inline HTML. Before text is rendered for visual editing, each
// HTML tag is swapped for an invisible marker (ten zero-width joiner and non-joiner characters
// encoding the tag's index), and the tags are put back after the edited text is exported. Both
// characters extend the preceding character, so the caret does not stop on them.
namespace htmlguard {

constexpr QChar kZero(0x200C);
constexpr QChar kOne(0x200D);
constexpr int kMarkerLength = 10;
constexpr int kMaxTags = 256;

inline QString marker(int index)
{
    QString text;
    text += kOne;
    text += kZero;
    for (int bit = 7; bit >= 0; --bit)
        text += ((index >> bit) & 1) ? kOne : kZero;
    return text;
}

// Returns the end (exclusive) of the code span that starts with the backtick run at `start`.
// An unmatched run is just the run itself.
inline qsizetype codeSpanEnd(const QString &text, qsizetype start)
{
    qsizetype run = 1;
    while (start + run < text.size() && text[start + run] == QLatin1Char('`'))
        ++run;
    qsizetype from = start + run;
    while (from < text.size()) {
        const qsizetype found = text.indexOf(QLatin1Char('`'), from);
        if (found < 0)
            break;
        qsizetype length = 1;
        while (found + length < text.size() && text[found + length] == QLatin1Char('`'))
            ++length;
        if (length == run)
            return found + length;
        from = found + length;
    }
    return start + run;
}

// Replaces inline HTML tags and comments outside code with markers and appends the tags to `tags`.
inline QString protect(const QString &text, QStringList *tags)
{
    static const QRegularExpression tag(
        QStringLiteral(R"((?:<!--.*?-->|</?[A-Za-z][A-Za-z0-9-]*(?:\s+[^<>]*)?/?>))"),
        QRegularExpression::DotMatchesEverythingOption);
    QString out;
    out.reserve(text.size());
    bool inFence = false;
    qsizetype i = 0;
    while (i < text.size()) {
        if (i == 0 || text[i - 1] == QLatin1Char('\n')) {
            qsizetype end = text.indexOf(QLatin1Char('\n'), i);
            end = end < 0 ? text.size() : end + 1;
            const QString line = text.mid(i, end - i).trimmed();
            if (line.startsWith(QLatin1String("```")) || line.startsWith(QLatin1String("~~~")))
                inFence = !inFence;
            if (inFence || line.startsWith(QLatin1String("```")) || line.startsWith(QLatin1String("~~~"))) {
                out += text.mid(i, end - i);
                i = end;
                continue;
            }
        }
        const QChar ch = text[i];
        if (ch == QLatin1Char('`')) {
            const qsizetype stop = codeSpanEnd(text, i);
            out += text.mid(i, stop - i);
            i = stop;
            continue;
        }
        if (ch == QLatin1Char('<') && tags->size() < kMaxTags) {
            const auto match = tag.match(text, i, QRegularExpression::NormalMatch, QRegularExpression::AnchorAtOffsetMatchOption);
            if (match.hasMatch()) {
                out += marker(tags->size());
                tags->push_back(match.captured(0));
                i += match.capturedLength(0);
                continue;
            }
        }
        out += ch;
        ++i;
    }
    return out;
}

// Replaces markers made by protect() with the tags they stand for.
inline QString restore(const QString &text, const QStringList &tags)
{
    QString out;
    out.reserve(text.size());
    qsizetype i = 0;
    while (i < text.size()) {
        if (text[i] == kOne && i + kMarkerLength <= text.size() && text[i + 1] == kZero) {
            int index = 0;
            bool valid = true;
            for (int bit = 0; bit < 8 && valid; ++bit) {
                const QChar digit = text[i + 2 + bit];
                valid = digit == kZero || digit == kOne;
                index = (index << 1) | (digit == kOne ? 1 : 0);
            }
            if (valid && index < tags.size()) {
                out += tags[index];
                i += kMarkerLength;
                continue;
            }
        }
        out += text[i];
        ++i;
    }
    return out;
}

// Qt's exporter escapes backslashes, asterisks and angle brackets inside code spans, where
// Markdown does not treat them specially. Undo that so code reads as the author wrote it.
inline QString unescapeCode(const QString &markdown)
{
    static const QRegularExpression escaped(QStringLiteral(R"(\\([\\*<]))"));
    QString out;
    out.reserve(markdown.size());
    bool inFence = false;
    qsizetype i = 0;
    while (i < markdown.size()) {
        if (i == 0 || markdown[i - 1] == QLatin1Char('\n')) {
            qsizetype end = markdown.indexOf(QLatin1Char('\n'), i);
            end = end < 0 ? markdown.size() : end + 1;
            const QString line = markdown.mid(i, end - i).trimmed();
            const bool fenceLine = line.startsWith(QLatin1String("```")) || line.startsWith(QLatin1String("~~~"));
            if (fenceLine)
                inFence = !inFence;
            if (inFence || fenceLine) {
                out += markdown.mid(i, end - i);
                i = end;
                continue;
            }
        }
        if (markdown[i] == QLatin1Char('`')) {
            const qsizetype stop = codeSpanEnd(markdown, i);
            QString span = markdown.mid(i, stop - i);
            span.replace(escaped, QStringLiteral("\\1"));
            out += span;
            i = stop;
            continue;
        }
        out += markdown[i];
        ++i;
    }
    return out;
}

}
