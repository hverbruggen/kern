#include "controller.h"
#include "htmlguard.h"

#include <QDir>
#include <QGuiApplication>
#include <QClipboard>
#include <QCursor>
#include <QTextDocument>
#include <QQuickTextDocument>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextList>
#include <QDesktopServices>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>

extern "C" {
void kern_free_string(char *);
char *kern_tables_json(const char *);
char *kern_replace_cell(const char *, size_t, size_t, size_t, const char *);
char *kern_table_command(const char *, size_t, const char *, size_t, size_t);
char *kern_new_table(size_t, size_t);
}

namespace {
QByteArray takeRustString(char *pointer)
{
    if (!pointer)
        return {};
    const QByteArray result(pointer);
    kern_free_string(pointer);
    return result;
}

QString displayName(const OpenDocument &document)
{
    return document.path.isEmpty() ? QStringLiteral("untitled") : QFileInfo(document.path).fileName();
}

bool dirty(const OpenDocument &document)
{
    return document.source != document.savedSource;
}

QString markdownFromRichHtml(const QString &html)
{
    QString cleanHtml = html;
    const QRegularExpression bodyTag(QStringLiteral("<body[^>]*>"));
    const auto body = bodyTag.match(cleanHtml);
    if (body.hasMatch()) {
        QString opening = body.captured();
        opening.replace(QRegularExpression(QStringLiteral("font-family:[^;]*;")),
                        QStringLiteral("font-family:'Sans Serif';"));
        cleanHtml.replace(body.capturedStart(), body.capturedLength(), opening);
    }
    QTextDocument formatted;
    formatted.setHtml(cleanHtml);
    return htmlguard::unescapeCode(formatted.toMarkdown(QTextDocument::MarkdownDialectGitHub));
}

// Markdown cannot hold an empty paragraph, so empty paragraphs at the very end of the last block
// are kept as extra blank lines after the final text: one blank line per empty paragraph.
int trailingEmptyParagraphs(const QString &html)
{
    static const QRegularExpression tail(QStringLiteral(
        R"((?:<p\b[^>]*-qt-paragraph-type:empty[^>]*>\s*<br\s*/?>\s*</p>\s*)+(?=</body>))"));
    const auto match = tail.match(html);
    if (!match.hasMatch())
        return 0;
    static const QRegularExpression paragraph(QStringLiteral(R"(<p\b)"));
    qsizetype count = 0;
    auto all = paragraph.globalMatch(match.captured());
    while (all.hasNext()) {
        all.next();
        ++count;
    }
    return int(qMin<qsizetype>(count, 50));
}

QJsonArray tablesFor(const QByteArray &source)
{
    return QJsonDocument::fromJson(takeRustString(kern_tables_json(source.constData()))).array();
}

bool isLatexFile(const QString &path)
{
    return QStringList{QStringLiteral("tex"), QStringLiteral("ltx"), QStringLiteral("latex")}
        .contains(QFileInfo(path).suffix().toLower());
}

QString latexPreview(const QString &source)
{
    static const QRegularExpression heading(QStringLiteral(R"(^\\(part|chapter|section|subsection|subsubsection)\{([^{}]*)\}\s*$)"));
    static const QRegularExpression inlineCommand(QStringLiteral(R"(\\(textbf|textit|emph|texttt)\{([^{}]*)\})"));
    static const QRegularExpression beginCode(QStringLiteral(R"(^\\begin\{(verbatim|listing|minted)\})"));
    static const QRegularExpression endCode(QStringLiteral(R"(^\\end\{(verbatim|listing|minted)\})"));
    QString html;
    bool code = false;
    const QStringList lines = source.split('\n');
    for (const QString &line : lines) {
        if (beginCode.match(line).hasMatch()) {
            html += QStringLiteral("<pre>");
            code = true;
            continue;
        }
        if (code && endCode.match(line).hasMatch()) {
            html += QStringLiteral("</pre>");
            code = false;
            continue;
        }
        if (code) {
            html += line.toHtmlEscaped() + QLatin1Char('\n');
            continue;
        }
        const auto head = heading.match(line);
        if (head.hasMatch()) {
            const QString command = head.captured(1);
            const int level = command == QStringLiteral("part") || command == QStringLiteral("chapter") ? 1
                : command == QStringLiteral("section") ? 2
                : command == QStringLiteral("subsection") ? 3 : 4;
            html += QStringLiteral("<h%1>%2</h%1>").arg(level).arg(head.captured(2).toHtmlEscaped());
            continue;
        }
        QString formatted;
        qsizetype offset = 0;
        auto matches = inlineCommand.globalMatch(line);
        while (matches.hasNext()) {
            const auto match = matches.next();
            formatted += line.mid(offset, match.capturedStart() - offset).toHtmlEscaped();
            const QString command = match.captured(1);
            const QString tag = command == QStringLiteral("textbf") ? QStringLiteral("b")
                : command == QStringLiteral("texttt") ? QStringLiteral("code") : QStringLiteral("i");
            formatted += QStringLiteral("<%1>%2</%1>").arg(tag, match.captured(2).toHtmlEscaped());
            offset = match.capturedEnd();
        }
        formatted += line.mid(offset).toHtmlEscaped();
        html += formatted + QStringLiteral("<br/>");
    }
    if (code)
        html += QStringLiteral("</pre>");
    return html;
}

constexpr int kMaxHistorySteps = 1000;
constexpr qsizetype kMaxHistoryBytes = 64 * 1024 * 1024;

void trimHistory(QVector<UndoStep> &history)
{
    qsizetype bytes = 0;
    for (const UndoStep &step : history)
        bytes += step.before.size() + step.after.size();
    while (history.size() > 1 && (history.size() > kMaxHistorySteps || bytes > kMaxHistoryBytes)) {
        bytes -= history.first().before.size() + history.first().after.size();
        history.removeFirst();
    }
}

// Splits Markdown into paragraph chunks. Each chunk keeps the blank lines that follow it, so
// joining the chunks reproduces the text. Blank lines inside fenced code do not split.
QStringList paragraphChunks(const QString &text)
{
    QStringList chunks;
    QString chunk;
    bool inFence = false;
    bool pendingBlank = false;
    for (const QString &line : text.split(QLatin1Char('\n'))) {
        const QString trimmed = line.trimmed();
        const bool blank = trimmed.isEmpty();
        if (pendingBlank && !blank && !inFence) {
            chunks.push_back(chunk);
            chunk.clear();
            pendingBlank = false;
        }
        if (!blank && (trimmed.startsWith(QLatin1String("```")) || trimmed.startsWith(QLatin1String("~~~"))))
            inFence = !inFence;
        if (!chunk.isEmpty() || !chunks.isEmpty() || !blank)
            chunk += line + QLatin1Char('\n');
        pendingBlank = blank && !inFence ? !chunk.isEmpty() : pendingBlank;
    }
    if (!chunk.isEmpty())
        chunks.push_back(chunk);
    return chunks;
}

QString normalizedMarkdown(const QString &markdown)
{
    QTextDocument document;
    document.setMarkdown(markdown);
    return htmlguard::unescapeCode(document.toMarkdown(QTextDocument::MarkdownDialectGitHub));
}

// Qt's Markdown exporter rewrites constructs it does not model (inline HTML, hard breaks,
// reference links, footnotes, escapes). To avoid damaging text the user did not touch, keep the
// original source of every paragraph the edit left alone.
//
// Each original paragraph exports to zero or more paragraphs of the normalized document. The
// edit is located by comparing the normalized original with the edited export from both ends;
// only the original paragraphs that overlap the changed range are replaced.
QString mergeEditedMarkdown(const QString &original, const QString &edited)
{
    // `edited` was exported from text whose HTML tags were swapped for markers (see htmlguard.h).
    QStringList tags;
    const QString guarded = htmlguard::protect(original, &tags);
    const QStringList originalChunks = paragraphChunks(original);
    const QStringList baseline = paragraphChunks(normalizedMarkdown(guarded));
    const QStringList editedChunks = paragraphChunks(edited);

    QVector<qsizetype> firstBaseline(originalChunks.size() + 1, 0);
    for (qsizetype i = 0; i < originalChunks.size(); ++i) {
        QStringList chunkTags;
        const QString chunk = htmlguard::protect(originalChunks[i], &chunkTags);
        firstBaseline[i + 1] = firstBaseline[i] + paragraphChunks(normalizedMarkdown(chunk)).size();
    }
    if (firstBaseline.last() != baseline.size())
        return htmlguard::restore(edited, tags);

    const qsizetype common = qMin(baseline.size(), editedChunks.size());
    qsizetype prefix = 0;
    while (prefix < common && baseline[prefix].trimmed() == editedChunks[prefix].trimmed())
        ++prefix;
    if (prefix == baseline.size() && prefix == editedChunks.size())
        return original;
    qsizetype suffix = 0;
    while (suffix < common - prefix
           && baseline[baseline.size() - 1 - suffix].trimmed() == editedChunks[editedChunks.size() - 1 - suffix].trimmed())
        ++suffix;

    // Widen the changed range to whole original paragraphs.
    auto ownerOf = [&](qsizetype baselineIndex) {
        qsizetype owner = 0;
        while (owner + 1 < originalChunks.size() && firstBaseline[owner + 1] <= baselineIndex)
            ++owner;
        return owner;
    };
    qsizetype firstChanged = prefix < baseline.size() ? ownerOf(prefix) : originalChunks.size();
    qsizetype lastChanged = baseline.size() - suffix > 0 ? ownerOf(baseline.size() - suffix - 1) : -1;
    if (lastChanged < firstChanged)
        lastChanged = firstChanged - 1;
    const qsizetype editedFrom = firstChanged < originalChunks.size() ? firstBaseline[firstChanged] : firstBaseline.last();
    const qsizetype editedTo = editedChunks.size() - (baseline.size() - firstBaseline[lastChanged + 1]);
    if (editedFrom > editedTo || editedTo > editedChunks.size())
        return htmlguard::restore(edited, tags);

    QString result;
    auto append = [&result](const QString &chunk) {
        if (!result.isEmpty() && !result.endsWith(QLatin1String("\n\n")))
            result += result.endsWith(QLatin1Char('\n')) ? QStringLiteral("\n") : QStringLiteral("\n\n");
        result += chunk;
    };
    for (qsizetype i = 0; i < firstChanged; ++i)
        append(originalChunks[i]);
    for (qsizetype i = editedFrom; i < editedTo; ++i)
        append(htmlguard::restore(editedChunks[i], tags));
    // Definitions (link references, footnotes) export to nothing; keep those inside the range.
    for (qsizetype i = firstChanged; i <= lastChanged && i < originalChunks.size(); ++i)
        if (firstBaseline[i + 1] == firstBaseline[i])
            append(originalChunks[i]);
    for (qsizetype i = lastChanged + 1; i < originalChunks.size(); ++i)
        append(originalChunks[i]);
    return result;
}

// Resolves a link target from a document. Returns an empty URL and sets `reason` when the link
// must not be opened. Only web and mail links and existing non-executable local files are allowed.
QUrl resolveLink(const QString &link, const QString &documentPath, QString *reason)
{
    const QUrl url(link.trimmed());
    const QString scheme = url.scheme().toLower();
    if (scheme == QLatin1String("http") || scheme == QLatin1String("https") || scheme == QLatin1String("mailto"))
        return url;
    QString path;
    if (scheme == QLatin1String("file")) {
        path = url.toLocalFile();
    } else if (scheme.isEmpty()) {
        // QUrl::path() is percent-decoded and excludes any #fragment or ?query.
        path = url.path();
        if (path.isEmpty()) {
            *reason = QStringLiteral("Links to headings within a document are not supported.");
            return {};
        }
        if (QDir::isRelativePath(path)) {
            if (documentPath.isEmpty()) {
                *reason = QStringLiteral("Save the document before opening links to local files.");
                return {};
            }
            path = QFileInfo(documentPath).absoluteDir().absoluteFilePath(path);
        }
    } else {
        *reason = QStringLiteral("Links of type \"") + scheme + QStringLiteral(":\" are not opened.");
        return {};
    }
    path = QDir::cleanPath(path);
    const QFileInfo target(path);
    if (!target.exists()) {
        *reason = QStringLiteral("File not found: ") + path;
        return {};
    }
    if (target.isFile() && (target.isExecutable() || target.suffix().compare(QLatin1String("desktop"), Qt::CaseInsensitive) == 0)) {
        *reason = QStringLiteral("Executable files are not opened from documents: ") + path;
        return {};
    }
    return QUrl::fromLocalFile(path);
}

// Index of the table that `after` has and `before` lacks: the first table whose position moved.
int newTableIndex(const QByteArray &before, const QByteArray &after)
{
    const QJsonArray was = tablesFor(before);
    const QJsonArray now = tablesFor(after);
    for (int i = 0; i < now.size(); ++i) {
        if (i >= was.size() || was[i].toObject().value(QStringLiteral("start")) != now[i].toObject().value(QStringLiteral("start")))
            return now.size() > was.size() ? i : -1;
    }
    return -1;
}

void appendFolder(const QString &path, int depth, const QSet<QString> &expanded, QSet<QString> &ancestors,
                  QVariantList &entries, int &remaining)
{
    if (depth > 32)
        return;
    const QString canonical = QFileInfo(path).canonicalFilePath();
    ancestors.insert(canonical);
    QDir folder(path);
    const auto children = folder.entryInfoList(QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot,
                                                QDir::DirsFirst | QDir::Name | QDir::IgnoreCase);
    for (const QFileInfo &entry : children) {
        if (remaining <= 0) {
            entries.push_back(QVariantMap{{"name", QStringLiteral("… more entries not shown")}, {"path", QString()},
                                          {"depth", depth}, {"directory", false}, {"expanded", false}, {"info", true}});
            break;
        }
        --remaining;
        entries.push_back(QVariantMap{{"name", entry.fileName()}, {"path", entry.absoluteFilePath()},
                                      {"depth", depth}, {"directory", entry.isDir()},
                                      {"expanded", expanded.contains(entry.absoluteFilePath())}});
        // A symlink back to a folder that is already open would otherwise recurse forever.
        if (entry.isDir() && expanded.contains(entry.absoluteFilePath())
            && !ancestors.contains(entry.canonicalFilePath()))
            appendFolder(entry.absoluteFilePath(), depth + 1, expanded, ancestors, entries, remaining);
    }
    ancestors.remove(canonical);
}
}

Controller::Controller(QObject *parent) : QObject(parent)
{
    connect(&m_folderWatcher, &QFileSystemWatcher::directoryChanged, this, [this] { rebuildProjectFiles(); });
}

OpenDocument *Controller::current()
{
    return m_current >= 0 && m_current < m_documents.size() ? &m_documents[m_current] : nullptr;
}

const OpenDocument *Controller::current() const
{
    return m_current >= 0 && m_current < m_documents.size() ? &m_documents[m_current] : nullptr;
}

QVariantList Controller::tabs() const
{
    QVariantList result;
    for (int i = 0; i < m_documents.size(); ++i) {
        const auto &document = m_documents[i];
        result.push_back(QVariantMap{{"name", displayName(document)}, {"path", document.path},
                                     {"dirty", dirty(document)}, {"active", i == m_current}});
    }
    return result;
}

QString Controller::title() const
{
    const auto *document = current();
    return document ? (dirty(*document) ? QStringLiteral("• ") : QString()) + displayName(*document) + QStringLiteral(" — kern")
                    : QStringLiteral("kern");
}

QString Controller::sourceText() const
{
    const auto *document = current();
    return document ? QString::fromUtf8(document->source) : QString();
}

QUrl Controller::documentUrl() const
{
    const auto *document = current();
    return document && !document->path.isEmpty() ? QUrl::fromLocalFile(document->path) : QUrl();
}

bool Controller::markdown() const
{
    const auto *document = current();
    return document && (document->path.isEmpty() || QStringList{QStringLiteral("md"), QStringLiteral("markdown")}
                             .contains(QFileInfo(document->path).suffix().toLower()));
}

bool Controller::hasDirtyDocuments() const
{
    for (const auto &document : m_documents)
        if (dirty(document))
            return true;
    return false;
}

void Controller::setError(const QString &message)
{
    m_error = message;
    emit errorChanged();
}

void Controller::clearUnsupportedFile()
{
    if (m_unsupportedFileMessage.isEmpty()) return;
    m_unsupportedFilePath.clear();
    m_unsupportedFileMessage.clear();
    emit unsupportedFileChanged();
}

void Controller::rebuildBlocks()
{
    m_blocks.clear();
    m_sourceBlocks.clear();
    const auto *document = current();
    if (!document) {
        emit blocksChanged();
        return;
    }
    const QByteArray &source = document->source;
    const QJsonArray tables = markdown() ? tablesFor(source) : QJsonArray();
    const bool latex = isLatexFile(document->path);
    auto appendText = [this, &source, latex](qsizetype start, qsizetype end) {
        const QString text = QString::fromUtf8(source.mid(start, end - start));
        m_sourceBlocks.push_back({start, end, -1});
        int trailingBlank = 0;
        if (!latex && end == source.size() && !text.trimmed().isEmpty()) {
            while (trailingBlank < text.size() && text[text.size() - trailingBlank - 1] == QLatin1Char('\n'))
                ++trailingBlank;
            trailingBlank = qBound(0, trailingBlank - 1, 50);
        }
        m_blocks.push_back(QVariantMap{{"kind", "text"}, {"text", text},
                                       {"preview", latex ? latexPreview(text) : text},
                                       {"previewFormat", latex ? "rich" : "source"},
                                       {"trailingBlank", trailingBlank}});
    };
    qsizetype offset = 0;
    for (int index = 0; index < tables.size(); ++index) {
        const QJsonObject table = tables[index].toObject();
        const qsizetype start = table.value(QStringLiteral("start")).toInteger();
        const qsizetype end = table.value(QStringLiteral("end")).toInteger();
        if (start < offset || end > source.size() || end < start)
            continue;
        if (start > offset) {
            appendText(offset, start);
        }
        QVariantList rows;
        rows.push_back(table.value(QStringLiteral("header")).toArray().toVariantList());
        for (const QJsonValue &row : table.value(QStringLiteral("rows")).toArray())
            rows.push_back(row.toArray().toVariantList());
        m_sourceBlocks.push_back({start, end, index});
        m_blocks.push_back(QVariantMap{{"kind", "table"}, {"tableIndex", index},
                                       {"rows", rows}, {"align", table.value(QStringLiteral("align")).toArray().toVariantList()},
                                       {"columns", table.value(QStringLiteral("header")).toArray().size()}});
        offset = end;
    }
    if (offset < source.size() || m_blocks.isEmpty() || m_sourceBlocks.last().tableIndex >= 0) {
        appendText(offset, source.size());
    }
    emit blocksChanged();
}

void Controller::changeSource(const QByteArray &newSource)
{
    auto *document = current();
    if (!document || document->source == newSource)
        return;
    const QByteArray &old = document->source;
    const qsizetype limit = qMin(old.size(), newSource.size());
    qsizetype prefix = 0;
    while (prefix < limit && old[prefix] == newSource[prefix])
        ++prefix;
    qsizetype suffix = 0;
    while (suffix < limit - prefix && old[old.size() - 1 - suffix] == newSource[newSource.size() - 1 - suffix])
        ++suffix;
    document->undo.push_back({prefix, old.mid(prefix, old.size() - prefix - suffix),
                              newSource.mid(prefix, newSource.size() - prefix - suffix)});
    trimHistory(document->undo);
    document->redo.clear();
    document->source = newSource;
    emit sourceChanged();
    emit tabsChanged();
    rebuildBlocks();
}

void Controller::newDocument()
{
    clearUnsupportedFile();
    m_documents.push_back(OpenDocument{});
    m_current = m_documents.size() - 1;
    emit tabsChanged();
    emit sourceChanged();
    rebuildBlocks();
}

bool Controller::openPath(const QString &path)
{
    clearUnsupportedFile();
    const QFileInfo info(path);
    if (!info.exists() || !info.isFile() || info.size() > 128 * 1024 * 1024) {
        setError(QStringLiteral("Cannot open file: missing, invalid, or larger than 128 MB: ") + path);
        return false;
    }
    const QString canonical = info.canonicalFilePath();
    for (int i = 0; i < m_documents.size(); ++i) {
        if (QFileInfo(m_documents[i].path).canonicalFilePath() == canonical) {
            selectTab(i);
            return true;
        }
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        setError(file.errorString());
        return false;
    }
    const QByteArray bytes = file.readAll();
    if (bytes.contains('\0') || QString::fromUtf8(bytes).toUtf8() != bytes) {
        m_unsupportedFilePath = canonical;
        m_unsupportedFileMessage = QStringLiteral("Not a UTF-8 text file: ") + path;
        emit unsupportedFileChanged();
        return false;
    }
    OpenDocument document;
    document.path = canonical;
    document.source = bytes;
    document.savedSource = bytes;
    document.diskModified = info.lastModified();
    m_documents.push_back(std::move(document));
    m_current = m_documents.size() - 1;
    emit tabsChanged();
    emit sourceChanged();
    rebuildBlocks();
    return true;
}

QString Controller::fileNameProblem(const QString &name) const
{
    if (name.trimmed().isEmpty() || name == QStringLiteral(".") || name == QStringLiteral("..")
        || name.contains(QLatin1Char('/')) || name.contains(QChar::Null)
        || name.contains(QLatin1Char('\n')) || name.contains(QLatin1Char('\r')))
        return QStringLiteral("Enter a valid filename without directory separators or line breaks.");
    return {};
}

QString Controller::createNamedProjectFile(const QUrl &folder, const QString &name)
{
    if (const QString problem = fileNameProblem(name); !problem.isEmpty())
        return problem;
    if (!folder.isLocalFile() || !QFileInfo(folder.toLocalFile()).isDir())
        return QStringLiteral("Select an existing destination directory.");
    const QString path = QDir(folder.toLocalFile()).filePath(name);
    if (QFileInfo::exists(path) || QFileInfo(path).isSymLink())
        return QStringLiteral("A file or directory with this name already exists.");
    // NewOnly also prevents an overwrite if a file appears after validation.
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::NewOnly))
        return QStringLiteral("Could not create file: ") + file.errorString();
    file.close();
    rebuildProjectFiles();
    return openPath(path) ? QString() : m_error;
}

void Controller::setProjectFolder(const QString &path)
{
    if (!QFileInfo(path).isDir()) {
        setError(QStringLiteral("Not a folder: ") + path);
        return;
    }
    m_projectFolder = QFileInfo(path).canonicalFilePath();
    m_expandedFolders.clear();
    rebuildProjectFiles();
}

void Controller::rebuildProjectFiles()
{
    m_projectFiles.clear();
    if (!m_folderWatcher.directories().isEmpty())
        m_folderWatcher.removePaths(m_folderWatcher.directories());
    QStringList watched;
    if (!m_projectFolder.isEmpty()) {
        watched.push_back(m_projectFolder);
        for (const QString &path : m_expandedFolders)
            if (QFileInfo(path).isDir())
                watched.push_back(path);
    }
    if (!watched.isEmpty())
        m_folderWatcher.addPaths(watched);
    int remaining = 2000;
    if (!m_projectFolder.isEmpty())
    {
            QSet<QString> ancestors;
            appendFolder(m_projectFolder, 0, m_expandedFolders, ancestors, m_projectFiles, remaining);
        }
    emit projectFilesChanged();
}

void Controller::activateProjectEntry(int index)
{
    if (index < 0 || index >= m_projectFiles.size())
        return;
    const QVariantMap entry = m_projectFiles[index].toMap();
    if (entry.value(QStringLiteral("info")).toBool())
        return;
    if (entry.value(QStringLiteral("directory")).toBool()) {
        const QString path = entry.value(QStringLiteral("path")).toString();
        if (m_expandedFolders.contains(path))
            m_expandedFolders.remove(path);
        else
            m_expandedFolders.insert(path);
        rebuildProjectFiles();
    } else {
        openPath(entry.value(QStringLiteral("path")).toString());
    }
}

void Controller::selectTab(int index)
{
    if (index < 0 || index >= m_documents.size()) return;
    clearUnsupportedFile();
    if (index == m_current) return;
    m_current = index;
    emit tabsChanged();
    emit sourceChanged();
    rebuildBlocks();
}

bool Controller::closeTab(int index, bool discard)
{
    if (index < 0 || index >= m_documents.size())
        return false;
    if (dirty(m_documents[index]) && !discard)
        return false;
    clearUnsupportedFile();
    m_documents.removeAt(index);
    if (m_documents.isEmpty())
        m_current = -1;
    else if (m_current >= m_documents.size())
        m_current = m_documents.size() - 1;
    else if (index < m_current)
        --m_current;
    emit tabsChanged();
    emit sourceChanged();
    rebuildBlocks();
    return true;
}

bool Controller::writeCurrent(const QString &path)
{
    auto *document = current();
    if (!document)
        return false;
    const QFileInfo existing(path);
    if (!document->path.isEmpty() && existing.exists()
        && existing.canonicalFilePath() == QFileInfo(document->path).canonicalFilePath()
        && existing.lastModified() > document->diskModified) {
        setError(QStringLiteral("File changed on disk. Reload it before saving: ") + path);
        return false;
    }
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(document->source) != document->source.size() || !file.commit()) {
        setError(QStringLiteral("Could not save ") + path + QStringLiteral(": ") + file.errorString());
        return false;
    }
    document->path = QFileInfo(path).absoluteFilePath();
    document->savedSource = document->source;
    document->diskModified = QFileInfo(path).lastModified();
    document->warnedDiskModified = {};
    emit tabsChanged();
    rebuildProjectFiles();
    return true;
}

bool Controller::save()
{
    auto *document = current();
    if (!document)
        return false;
    return !document->path.isEmpty() && writeCurrent(document->path);
}

bool Controller::reloadCurrent(bool discard)
{
    auto *document = current();
    if (!document || document->path.isEmpty() || (dirty(*document) && !discard))
        return false;
    QFile file(document->path);
    if (!file.open(QIODevice::ReadOnly)) {
        setError(QStringLiteral("Could not reload ") + document->path + QStringLiteral(": ") + file.errorString());
        return false;
    }
    const QByteArray bytes = file.readAll();
    if (bytes.contains('\0') || QString::fromUtf8(bytes).toUtf8() != bytes) {
        setError(QStringLiteral("Not a UTF-8 text file: ") + document->path);
        return false;
    }
    document->source = bytes;
    document->savedSource = bytes;
    document->undo.clear();
    document->redo.clear();
    document->diskModified = QFileInfo(document->path).lastModified();
    document->warnedDiskModified = {};
    emit tabsChanged();
    emit sourceChanged();
    rebuildBlocks();
    return true;
}

void Controller::undo()
{
    auto *document = current();
    if (!document || document->undo.isEmpty())
        return;
    const UndoStep step = document->undo.takeLast();
    document->source.replace(step.start, step.after.size(), step.before);
    document->redo.push_back(step);
    emit tabsChanged();
    emit sourceChanged();
    rebuildBlocks();
}

void Controller::redo()
{
    auto *document = current();
    if (!document || document->redo.isEmpty())
        return;
    const UndoStep step = document->redo.takeLast();
    document->source.replace(step.start, step.before.size(), step.after);
    document->undo.push_back(step);
    emit tabsChanged();
    emit sourceChanged();
    rebuildBlocks();
}

void Controller::editTextBlock(int block, const QString &text)
{
    auto *document = current();
    if (!document || block < 0 || block >= m_sourceBlocks.size())
        return;
    if (text.contains(QChar::Null)) {
        setError(QStringLiteral("Text files cannot contain NUL characters."));
        return;
    }
    const SourceBlock item = m_sourceBlocks[block];
    if (item.tableIndex >= 0)
        return;
    QByteArray source = document->source;
    source.replace(item.start, item.end - item.start, text.toUtf8());
    changeSource(source);
}

void Controller::editFormattedBlock(int block, const QString &html)
{
    if (!markdown())
        return;
    QString replacement = markdownFromRichHtml(html);
    const auto *document = current();
    if (!document || block < 0 || block >= m_sourceBlocks.size())
        return;
    const SourceBlock item = m_sourceBlocks[block];
    if (item.tableIndex >= 0)
        return;
    const QString original = QString::fromUtf8(document->source.mid(item.start, item.end - item.start));
    int leadingNewlines = 0;
    while (leadingNewlines < original.size() && original[leadingNewlines] == QLatin1Char('\n'))
        ++leadingNewlines;
    int trailingNewlines = 0;
    while (trailingNewlines < original.size() && original[original.size() - trailingNewlines - 1] == QLatin1Char('\n'))
        ++trailingNewlines;
    while (replacement.endsWith(QLatin1Char('\n')))
        replacement.chop(1);
    while (replacement.startsWith(QLatin1Char('\n')))
        replacement.remove(0, 1);
    if (!original.trimmed().isEmpty()) {
        const QString core = original.mid(leadingNewlines, original.size() - leadingNewlines - trailingNewlines);
        replacement = mergeEditedMarkdown(core, replacement);
        while (replacement.endsWith(QLatin1Char('\n')))
            replacement.chop(1);
    }
    // The empty text block after a final table needs a paragraph separator on first edit.
    if (original.isEmpty() && !replacement.isEmpty() && block > 0
        && m_sourceBlocks[block - 1].tableIndex >= 0) {
        int precedingNewlines = 0;
        for (qsizetype i = item.start; i > 0 && document->source[i - 1] == '\n'; --i)
            ++precedingNewlines;
        leadingNewlines = qMax(0, 2 - precedingNewlines);
    }
    if (block == m_sourceBlocks.size() - 1 && !replacement.isEmpty()) {
        const int emptyAtEnd = trailingEmptyParagraphs(html);
        trailingNewlines = emptyAtEnd > 0 ? emptyAtEnd + 1 : qMin(trailingNewlines, 1);
    }
    replacement.prepend(QString(leadingNewlines, QLatin1Char('\n')));
    replacement += QString(trailingNewlines, QLatin1Char('\n'));
    editTextBlock(block, replacement);
}

void Controller::editFormattedCell(int table, int row, int col, const QString &html)
{
    // The cell was shown with its HTML tags swapped for markers; put the same tags back.
    QString source;
    for (const QVariant &block : std::as_const(m_blocks)) {
        const QVariantMap map = block.toMap();
        if (map.value(QStringLiteral("kind")).toString() == QLatin1String("table")
            && map.value(QStringLiteral("tableIndex")).toInt() == table) {
            const QVariantList rows = map.value(QStringLiteral("rows")).toList();
            if (row >= 0 && row < rows.size()) {
                const QVariantList cells = rows[row].toList();
                if (col >= 0 && col < cells.size())
                    source = cells[col].toString();
            }
            break;
        }
    }
    QStringList tags;
    htmlguard::protect(source, &tags);
    QString replacement = htmlguard::restore(markdownFromRichHtml(html), tags);
    while (replacement.endsWith(QLatin1Char('\n')))
        replacement.chop(1);
    editCell(table, row, col, replacement);
}

void Controller::formatSelection(QObject *quickDocument, int start, int end, const QString &style)
{
    auto *wrapper = qobject_cast<QQuickTextDocument *>(quickDocument);
    if (!wrapper || !wrapper->textDocument() || start < 0 || end <= start)
        return;
    QTextCursor cursor(wrapper->textDocument());
    cursor.setPosition(start);
    cursor.setPosition(end, QTextCursor::KeepAnchor);
    QTextCharFormat format;
    if (style == QStringLiteral("bold"))
        format.setFontWeight(cursor.charFormat().fontWeight() >= QFont::Bold ? QFont::Normal : QFont::Bold);
    else if (style == QStringLiteral("italic"))
        format.setFontItalic(!cursor.charFormat().fontItalic());
    else
        return;
    cursor.mergeCharFormat(format);
}

bool Controller::styleAtCursor(QObject *quickDocument, int position, const QString &style) const
{
    auto *wrapper = qobject_cast<QQuickTextDocument *>(quickDocument);
    if (!wrapper || !wrapper->textDocument())
        return false;
    QTextCursor cursor(wrapper->textDocument());
    cursor.setPosition(qBound(0, position, wrapper->textDocument()->characterCount() - 1));
    const QTextCharFormat format = cursor.charFormat();
    return style == QStringLiteral("bold") ? format.fontWeight() >= QFont::Bold : format.fontItalic();
}

int Controller::insertStyledText(QObject *quickDocument, int start, int end, const QString &text,
                                 bool bold, bool italic)
{
    auto *wrapper = qobject_cast<QQuickTextDocument *>(quickDocument);
    if (!wrapper || !wrapper->textDocument())
        return start;
    QTextDocument *document = wrapper->textDocument();
    QTextCursor cursor(document);
    cursor.setPosition(qBound(0, start, document->characterCount() - 1));
    cursor.setPosition(qBound(0, end, document->characterCount() - 1), QTextCursor::KeepAnchor);
    QTextCharFormat format = cursor.charFormat();
    format.setFontWeight(bold ? QFont::Bold : QFont::Normal);
    format.setFontItalic(italic);
    cursor.insertText(text, format);
    return cursor.position();
}

int Controller::applyHeadingShortcut(QObject *quickDocument, int position)
{
    auto *wrapper = qobject_cast<QQuickTextDocument *>(quickDocument);
    if (!wrapper || !wrapper->textDocument())
        return -1;
    QTextDocument *document = wrapper->textDocument();
    QTextCursor cursor(document);
    cursor.setPosition(qBound(0, position, document->characterCount() - 1));
    const QTextBlock block = cursor.block();
    const QString text = block.text();
    static const QRegularExpression hashes(QStringLiteral("^#{1,6}$"));
    if (position - block.position() != text.size() || !hashes.match(text).hasMatch())
        return -1;
    // Take the look of a heading from Qt's own Markdown importer so edits and reloads agree.
    QTextDocument reference;
    reference.setMarkdown(QString(text.size(), QLatin1Char('#')) + QStringLiteral(" x"));
    const QTextBlock headingBlock = reference.firstBlock();
    QTextBlockFormat blockFormat = headingBlock.blockFormat();
    const QTextCharFormat charFormat = headingBlock.begin().fragment().charFormat();
    blockFormat.setTopMargin(block == document->begin() ? 4 : 16);
    blockFormat.setBottomMargin(10);
    cursor.setPosition(block.position());
    cursor.setPosition(block.position() + text.size(), QTextCursor::KeepAnchor);
    cursor.removeSelectedText();
    cursor.setBlockFormat(blockFormat);
    cursor.setBlockCharFormat(charFormat);
    cursor.setCharFormat(charFormat);
    return cursor.position();
}

int Controller::endHeading(QObject *quickDocument, int position)
{
    auto *wrapper = qobject_cast<QQuickTextDocument *>(quickDocument);
    if (!wrapper || !wrapper->textDocument())
        return -1;
    QTextDocument *document = wrapper->textDocument();
    QTextCursor cursor(document);
    cursor.setPosition(qBound(0, position, document->characterCount() - 1));
    const QTextBlock block = cursor.block();
    if (block.blockFormat().headingLevel() == 0 || position - block.position() != block.text().size()
        || block.text().isEmpty())
        return -1;
    QTextBlockFormat paragraph;
    paragraph.setTopMargin(0);
    paragraph.setBottomMargin(9);
    cursor.insertBlock(paragraph, QTextCharFormat());
    return cursor.position();
}

void Controller::toggleList(QObject *quickDocument, int start, int end, bool numbered)
{
    auto *wrapper = qobject_cast<QQuickTextDocument *>(quickDocument);
    if (!wrapper || !wrapper->textDocument())
        return;
    QTextDocument *document = wrapper->textDocument();
    QTextCursor cursor(document);
    cursor.setPosition(qBound(0, start, document->characterCount() - 1));
    cursor.setPosition(qBound(0, end, document->characterCount() - 1), QTextCursor::KeepAnchor);
    const auto style = numbered ? QTextListFormat::ListDecimal : QTextListFormat::ListDisc;
    if (QTextList *list = cursor.currentList(); list && list->format().style() == style)
        list->remove(cursor.block());
    else
        cursor.createList(style);
}

int Controller::insertLink(QObject *quickDocument, int start, int end, const QString &url,
                           const QString &label, const QColor &color)
{
    auto *wrapper = qobject_cast<QQuickTextDocument *>(quickDocument);
    if (!wrapper || !wrapper->textDocument() || url.isEmpty())
        return start;
    QTextDocument *document = wrapper->textDocument();
    QTextCursor cursor(document);
    cursor.setPosition(qBound(0, start, document->characterCount() - 1));
    cursor.setPosition(qBound(0, end, document->characterCount() - 1), QTextCursor::KeepAnchor);
    QTextCharFormat format = cursor.charFormat();
    format.setAnchor(true);
    format.setAnchorHref(url);
    format.setForeground(color);
    format.setFontUnderline(true);
    const QString display = label.isEmpty() ? (cursor.hasSelection() ? cursor.selectedText() : url) : label;
    cursor.insertText(display, format);
    return cursor.position();
}

int Controller::insertImage(QObject *quickDocument, int start, int end, const QString &url,
                            const QString &altText)
{
    auto *wrapper = qobject_cast<QQuickTextDocument *>(quickDocument);
    if (!wrapper || !wrapper->textDocument() || url.isEmpty())
        return start;
    QTextDocument *document = wrapper->textDocument();
    QTextCursor cursor(document);
    cursor.setPosition(qBound(0, start, document->characterCount() - 1));
    cursor.setPosition(qBound(0, end, document->characterCount() - 1), QTextCursor::KeepAnchor);
    QTextImageFormat format;
    format.setName(url);
    format.setProperty(QTextFormat::ImageAltText, altText);
    cursor.insertImage(format);
    return cursor.position();
}

void Controller::editCell(int table, int row, int col, const QString &text)
{
    const auto *document = current();
    if (!document || table < 0 || row < 0 || col < 0)
        return;
    const QByteArray replacement = text.toUtf8();
    char *edited = kern_replace_cell(document->source.constData(), table, row, col, replacement.constData());
    if (edited)
        changeSource(takeRustString(edited));
    else
        setError(text.contains('\n') || text.contains('\r')
            ? QStringLiteral("Markdown table cells cannot contain line breaks.")
            : QStringLiteral("Could not edit Markdown table cell."));
}

bool Controller::deleteTable(int tableIndex)
{
    const auto *document = current();
    if (!document || !markdown() || tableIndex < 0)
        return false;
    const QJsonArray tables = tablesFor(document->source);
    if (tableIndex >= tables.size())
        return false;
    const QJsonObject table = tables[tableIndex].toObject();
    const qsizetype start = table.value(QStringLiteral("start")).toInteger();
    qsizetype end = table.value(QStringLiteral("end")).toInteger();
    QByteArray source = document->source;
    // Take the table's line ending too, and one blank line so the neighbours do not drift apart.
    if (end < source.size() && source[end] == '\r')
        ++end;
    if (end < source.size() && source[end] == '\n')
        ++end;
    auto atBlankLine = [&source](qsizetype at) {
        return at < source.size() && (source[at] == '\n' || (source[at] == '\r' && at + 1 < source.size() && source[at + 1] == '\n'));
    };
    const bool blankBefore = start == 0 || (start >= 2 && source[start - 1] == '\n' && source[start - 2] == '\n');
    if (blankBefore && atBlankLine(end))
        end += source[end] == '\r' ? 2 : 1;
    source.remove(start, end - start);
    changeSource(source);
    return true;
}

void Controller::tableCommand(int table, const QString &command, int row, int col)
{
    const auto *document = current();
    if (!document || table < 0 || row < 0 || col < 0)
        return;
    const QByteArray commandBytes = command.toUtf8();
    char *edited = kern_table_command(document->source.constData(), table, commandBytes.constData(), row, col);
    if (edited)
        changeSource(takeRustString(edited));
}

int Controller::applyTableInsertion(const QByteArray &newSource)
{
    const auto *document = current();
    if (!document)
        return -1;
    const QByteArray before = document->source;
    changeSource(newSource);
    return newTableIndex(before, newSource);
}

int Controller::insertTable(int block, int cursorUtf16, int rows, int cols)
{
    const auto *document = current();
    if (!document || block < 0 || block >= m_sourceBlocks.size() || rows < 1 || cols < 1 || rows > 100 || cols > 30)
        return -1;
    const SourceBlock item = m_sourceBlocks[block];
    if (item.tableIndex >= 0)
        return -1;
    const QString original = QString::fromUtf8(document->source.mid(item.start, item.end - item.start));
    const qsizetype position = item.start + original.left(qBound(0, cursorUtf16, original.size())).toUtf8().size();
    const QByteArray table = takeRustString(kern_new_table(rows, cols));
    QByteArray source = document->source;
    source.insert(position, QByteArray("\n\n") + table + QByteArray("\n\n"));
    return applyTableInsertion(source);
}

int Controller::insertTableAtVisualCursor(int block, int cursorPosition, int rows, int cols)
{
    const auto *document = current();
    if (!document || !markdown() || block < 0 || block >= m_sourceBlocks.size()
        || rows < 1 || cols < 1 || rows > 100 || cols > 30)
        return -1;
    const SourceBlock item = m_sourceBlocks[block];
    if (item.tableIndex >= 0)
        return -1;
    const QString original = QString::fromUtf8(document->source.mid(item.start, item.end - item.start));
    QTextDocument before;
    before.setMarkdown(original);
    QTextDocument after;
    after.setMarkdown(original);
    const int position = qBound(0, cursorPosition, before.characterCount() - 1);
    QTextCursor beforeCursor(&before);
    beforeCursor.setPosition(position);
    beforeCursor.movePosition(QTextCursor::End, QTextCursor::KeepAnchor);
    beforeCursor.removeSelectedText();
    QTextCursor afterCursor(&after);
    afterCursor.setPosition(position);
    afterCursor.setPosition(0, QTextCursor::KeepAnchor);
    afterCursor.removeSelectedText();
    const QString left = before.toMarkdown(QTextDocument::MarkdownDialectGitHub).trimmed();
    const QString right = after.toMarkdown(QTextDocument::MarkdownDialectGitHub).trimmed();
    const QString table = QString::fromUtf8(takeRustString(kern_new_table(rows, cols))).trimmed();
    QString replacement;
    if (!left.isEmpty())
        replacement += left + QStringLiteral("\n\n");
    else if (item.start > 0)
        replacement += QStringLiteral("\n\n");
    replacement += table + QStringLiteral("\n\n");
    if (!right.isEmpty())
        replacement += right + QLatin1Char('\n');
    QByteArray source = document->source;
    source.replace(item.start, item.end - item.start, replacement.toUtf8());
    return applyTableInsertion(source);
}

int Controller::insertTableAtSourceCursor(int cursorUtf16, int rows, int cols)
{
    const auto *document = current();
    if (!document || rows < 1 || cols < 1 || rows > 100 || cols > 30)
        return -1;
    const QString original = QString::fromUtf8(document->source);
    const qsizetype position = original.left(qBound(0, cursorUtf16, original.size())).toUtf8().size();
    const QByteArray table = takeRustString(kern_new_table(rows, cols));
    QByteArray source = document->source;
    source.insert(position, QByteArray("\n\n") + table + QByteArray("\n\n"));
    return applyTableInsertion(source);
}

int Controller::insertTableAfterTable(int tableIndex, int rows, int cols)
{
    const auto *document = current();
    if (!document || !markdown() || tableIndex < 0 || rows < 1 || cols < 1 || rows > 100 || cols > 30)
        return -1;
    const QJsonArray tables = tablesFor(document->source);
    if (tableIndex >= tables.size())
        return -1;
    const qsizetype position = tables[tableIndex].toObject().value(QStringLiteral("end")).toInteger();
    const QByteArray table = takeRustString(kern_new_table(rows, cols));
    QByteArray source = document->source;
    source.insert(position, QByteArray("\n\n") + table + QByteArray("\n\n"));
    return applyTableInsertion(source);
}

void Controller::setSourceText(const QString &text)
{
    if (text.contains(QChar::Null)) {
        setError(QStringLiteral("Text files cannot contain NUL characters."));
        return;
    }
    changeSource(text.toUtf8());
}

void Controller::openLink(const QString &link)
{
    const auto *document = current();
    QString reason;
    const QUrl url = resolveLink(link, document ? document->path : QString(), &reason);
    if (url.isEmpty())
        setError(reason);
    else if (!QDesktopServices::openUrl(url))
        setError(QStringLiteral("Could not open ") + url.toString());
}

void Controller::copyLink(const QString &link)
{
    // Copy what the link points to, without checking that it can be opened.
    const auto *document = current();
    QUrl url(link.trimmed());
    if (url.scheme().isEmpty() && !url.path().isEmpty() && document && !document->path.isEmpty())
        url = QUrl::fromLocalFile(QDir::cleanPath(QFileInfo(document->path).absoluteDir().absoluteFilePath(url.path())));
    QGuiApplication::clipboard()->setText(url.toString());
}

QPointF Controller::pointerPosition() const
{
    return QCursor::pos();
}

bool Controller::checkDiskChanges()
{
    bool changed = false;
    for (auto &document : m_documents) {
        if (document.path.isEmpty())
            continue;
        const QFileInfo info(document.path);
        if (!info.exists() || info.lastModified() <= document.diskModified)
            continue;
        changed = true;
        if (document.warnedDiskModified != info.lastModified()) {
            setError(QStringLiteral("File changed on disk. Use Reload from Disk to review it: ") + document.path);
            document.warnedDiskModified = info.lastModified();
        }
    }
    return changed;
}
