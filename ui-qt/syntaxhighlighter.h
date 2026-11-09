#pragma once
#include <QSyntaxHighlighter>
#include <QRegularExpression>
#include <QTextDocument>
#include <QTextBlock>
#include <QColor>

// Presentation only: never insert, remove or serialize characters from a decorated document.
class SyntaxHighlighter final : public QSyntaxHighlighter {
public:
    SyntaxHighlighter(QTextDocument *document, QColor accent, QColor muted, QColor surface)
        : QSyntaxHighlighter(document), accent(accent), muted(muted), surface(surface) {}
private:
    QColor accent, muted, surface;
    void codeTokens(const QString &text) {
        auto paint = [&](const QString &pattern, QColor color) {
            QTextCharFormat f; f.setForeground(color); f.setBackground(surface);
            auto matches = QRegularExpression(pattern).globalMatch(text);
            while (matches.hasNext()) { const auto m=matches.next(); setFormat(m.capturedStart(),m.capturedLength(),f); }
        };
        paint(QStringLiteral(R"(\b(?:if|else|for|while|return|class|struct|def|fn|let|const|var|import|from|include|public|private|true|false|null|None|True|False|int|float|double|void|auto|QString)\b)"),accent);
        paint(QStringLiteral(R"(\b[0-9]+(?:\.[0-9]+)?\b)"),accent);
        paint(QStringLiteral(R"("(?:[^"\\]|\\.)*"|'(?:[^'\\]|\\.)*')"),accent);
        paint(QStringLiteral(R"(//.*$|#.*$)"),muted);
    }
    void highlightBlock(const QString &text) override {
        QTextCharFormat marker, code, emphasis, math;
        marker.setForeground(muted);
        code.setBackground(surface);
        math.setForeground(accent);
        math.setFontItalic(true);
        setCurrentBlockState(0);
        const auto fence = QRegularExpression(QStringLiteral("^ {0,3}(`{3,}|~{3,})(.*)$")).match(text);
        const int previous = previousBlockState();
        if (previous > 0 && previous != 1) {
            setFormat(0, text.size(), code);
            codeTokens(text);
            const int count = previous >> 1;
            const QChar ch = (previous & 1) ? QLatin1Char('~') : QLatin1Char('`');
            if (fence.hasMatch() && fence.captured(1)[0] == ch && fence.captured(1).size() >= count
                && fence.captured(2).trimmed().isEmpty()) setFormat(0, text.size(), marker);
            else setCurrentBlockState(previous);
            return;
        }
        if (fence.hasMatch() && !(fence.captured(1)[0] == QLatin1Char('`') && fence.captured(2).contains('`'))) {
            setCurrentBlockState((fence.captured(1).size() << 1) | (fence.captured(1)[0] == QLatin1Char('~')));
            setFormat(0, text.size(), marker);
            return;
        }
        if (previous == 1 || text.trimmed() == QStringLiteral("$$") || text.trimmed() == QStringLiteral("\\[")) {
            setFormat(0, text.size(), math);
            setCurrentBlockState(previous == 1 && (text.trimmed() == QStringLiteral("$$") || text.trimmed() == QStringLiteral("\\]")) ? 0 : 1);
            return;
        }
        if (text.startsWith(QStringLiteral("    ")) || text.startsWith('\t')) {
            setFormat(0, text.size(), code);
            return;
        }
        if (QRegularExpression(QStringLiteral("^ {0,3}(=+|-+)\\s*$")).match(currentBlock().next().text()).hasMatch()
            && !text.trimmed().isEmpty()) {
            QTextCharFormat title; title.setFontWeight(QFont::Bold);
            title.setProperty(QTextFormat::FontSizeAdjustment,2); setFormat(0,text.size(),title);
        }
        const auto heading = QRegularExpression(QStringLiteral("^(#{1,6})\\s+.+")).match(text);
        if (heading.hasMatch()) {
            QTextCharFormat title;
            title.setFontWeight(QFont::Bold);
            title.setProperty(QTextFormat::FontSizeAdjustment, 4 - heading.captured(1).size());
            setFormat(0, text.size(), title);
            setFormat(0, heading.captured(1).size(), marker);
        }
        auto apply = [&](const QString &pattern, const QTextCharFormat &format) {
            auto matches = QRegularExpression(pattern).globalMatch(text);
            while (matches.hasNext()) {
                const auto m = matches.next();
                setFormat(m.capturedStart(), m.capturedLength(), format);
            }
        };
        emphasis.setFontWeight(QFont::Bold);
        apply(QStringLiteral(R"((?<!\\)\*\*[^*]+\*\*|__[^_]+__)"), emphasis);
        emphasis = QTextCharFormat(); emphasis.setFontItalic(true);
        apply(QStringLiteral(R"((?<![\\*])\*[^*]+\*(?!\*)|(?<![\\_])_[^_]+_(?!_))"), emphasis);
        apply(QStringLiteral(R"(^\s*(?:>+|[-+*]|\d+[.)])\s|\[[ xX]\]|!?\[[^\]]*\]\([^)]*\))"), marker);
        apply(QStringLiteral(R"((?<!\\)\$[^$\n]+\$|\\[a-zA-Z]+|[{}])"), math);
        // Code wins over Markdown and math styling.
        apply(QStringLiteral(R"((`+)(.+?)\1)"), code);
    }
};
