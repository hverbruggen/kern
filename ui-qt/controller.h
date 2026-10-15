#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QPointF>
#include <QDir>
#include <QFileInfo>
#include <QObject>
#include <QUrl>
#include <QSet>
#include <QFileSystemWatcher>
#include <QString>
#include <QVariantList>
#include <QVector>

// One edit, stored as the bytes it replaced so history costs memory in proportion to the edit.
struct UndoStep {
    qsizetype start = 0;
    QByteArray before;
    QByteArray after;
};

struct OpenDocument {
    QString path;
    QByteArray source;
    QByteArray savedSource;
    QVector<UndoStep> undo;
    QVector<UndoStep> redo;
    QDateTime diskModified;
    QDateTime warnedDiskModified;
};

struct SourceBlock {
    qsizetype start;
    qsizetype end;
    int tableIndex;
};

class Controller : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList tabs READ tabs NOTIFY tabsChanged)
    Q_PROPERTY(QVariantList blocks READ blocks NOTIFY blocksChanged)
    Q_PROPERTY(QVariantList projectFiles READ projectFiles NOTIFY projectFilesChanged)
    Q_PROPERTY(QStringList recentFolders READ recentFolders NOTIFY recentFoldersChanged)
    Q_PROPERTY(QString homePath READ homePath CONSTANT)
    Q_PROPERTY(QString projectFolder READ projectFolder NOTIFY projectFilesChanged)
    Q_PROPERTY(QString title READ title NOTIFY tabsChanged)
    Q_PROPERTY(QString sourceText READ sourceText NOTIFY sourceChanged)
    Q_PROPERTY(QUrl documentUrl READ documentUrl NOTIFY tabsChanged)
    Q_PROPERTY(QUrl projectFolderUrl READ projectFolderUrl NOTIFY projectFilesChanged)
    Q_PROPERTY(QUrl suggestedSaveUrl READ suggestedSaveUrl NOTIFY tabsChanged)
    Q_PROPERTY(bool currentHasPath READ currentHasPath NOTIFY tabsChanged)
    Q_PROPERTY(bool hasDocument READ hasDocument NOTIFY tabsChanged)
    Q_PROPERTY(bool markdown READ markdown NOTIFY tabsChanged)
    Q_PROPERTY(bool hasDirtyDocuments READ hasDirtyDocuments NOTIFY tabsChanged)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)
    Q_PROPERTY(QString unsupportedFilePath READ unsupportedFilePath NOTIFY unsupportedFileChanged)
    Q_PROPERTY(QString unsupportedFileMessage READ unsupportedFileMessage NOTIFY unsupportedFileChanged)
public:
    explicit Controller(QObject *parent = nullptr);
    QVariantList tabs() const;
    QVariantList blocks() const { return m_blocks; }
    QVariantList projectFiles() const { return m_projectFiles; }
    QString projectFolder() const { return m_projectFolder; }
    QString title() const;
    QString sourceText() const;
    QUrl documentUrl() const;
    QUrl projectFolderUrl() const { return QUrl::fromLocalFile(m_projectFolder.isEmpty() ? QDir::homePath() : m_projectFolder); }
    QUrl suggestedSaveUrl() const {
        const auto *document = current();
        return QUrl::fromLocalFile(document && !document->path.isEmpty() ? document->path
            : QDir(m_projectFolder.isEmpty() ? QDir::homePath() : m_projectFolder).filePath(QStringLiteral("untitled.md")));
    }
    bool currentHasPath() const { return current() && !current()->path.isEmpty(); }
    bool hasDocument() const { return current() != nullptr; }
    bool markdown() const;
    bool hasDirtyDocuments() const;
    QString error() const { return m_error; }
    QString unsupportedFilePath() const { return m_unsupportedFilePath; }
    QString unsupportedFileMessage() const { return m_unsupportedFileMessage; }

    Q_INVOKABLE void newDocument();
    Q_INVOKABLE bool openUrl(const QUrl &url) { return url.isLocalFile() && openPath(url.toLocalFile()); }
    Q_INVOKABLE bool openPath(const QString &path);
    Q_INVOKABLE void setProjectFolderUrl(const QUrl &url) { if (url.isLocalFile()) setProjectFolder(url.toLocalFile()); }
    Q_INVOKABLE QUrl existingFolderUrl(const QString &path) const {
        const QFileInfo folder(path);
        return folder.isDir() ? QUrl::fromLocalFile(folder.canonicalFilePath()) : QUrl();
    }
    Q_INVOKABLE QString localFolderPath(const QUrl &url) const { return url.toLocalFile(); }
    Q_INVOKABLE QString createNamedProjectFile(const QUrl &folder, const QString &name);
    // Why `name` cannot be a file name in one directory, or an empty string when it can.
    Q_INVOKABLE QString fileNameProblem(const QString &name) const;
    Q_INVOKABLE QUrl folderOf(const QUrl &file) const { return QUrl::fromLocalFile(QFileInfo(file.toLocalFile()).absolutePath()); }
    Q_INVOKABLE QString fileNameOf(const QUrl &file) const { return QFileInfo(file.toLocalFile()).fileName(); }
    Q_INVOKABLE QUrl childUrl(const QUrl &folder, const QString &name) const { return QUrl::fromLocalFile(QDir(folder.toLocalFile()).filePath(name)); }
    Q_INVOKABLE bool pathExists(const QUrl &url) const { return QFileInfo::exists(url.toLocalFile()) || QFileInfo(url.toLocalFile()).isSymLink(); }
    Q_INVOKABLE bool isFile(const QUrl &url) const { return QFileInfo(url.toLocalFile()).isFile(); }
    Q_INVOKABLE void setProjectFolder(const QString &path);
    // Folders opened before, newest first, at most ten; only ones that still exist are listed.
    QStringList recentFolders() const;
    QString homePath() const { return QDir::homePath(); }
    void setRememberFolders(bool remember);
    // Folders can disappear while kern runs, so the menu re-reads the list when it opens.
    Q_INVOKABLE void refreshRecentFolders() { emit recentFoldersChanged(); }
    Q_INVOKABLE void activateProjectEntry(int index);
    Q_INVOKABLE void selectTab(int index);
    Q_INVOKABLE bool closeTab(int index, bool discard = false);
    Q_INVOKABLE bool save();
    Q_INVOKABLE bool saveAsUrl(const QUrl &url) { return url.isLocalFile() && writeCurrent(url.toLocalFile()); }
    Q_INVOKABLE bool reloadCurrent(bool discard = false);
    Q_INVOKABLE void undo();
    Q_INVOKABLE void redo();
    Q_INVOKABLE void editTextBlock(int block, const QString &text);
    Q_INVOKABLE void editFormattedBlock(int block, const QString &html);
    Q_INVOKABLE void editFormattedCell(int table, int row, int col, const QString &html);
    Q_INVOKABLE void formatSelection(QObject *quickDocument, int start, int end, const QString &style);
    Q_INVOKABLE bool styleAtCursor(QObject *quickDocument, int position, const QString &style) const;
    Q_INVOKABLE int insertStyledText(QObject *quickDocument, int start, int end, const QString &text,
                                    bool bold, bool italic);
    // Typing "## " at the start of a paragraph turns it into a heading. Returns the new caret
    // position, or -1 when the caret is not right after the hashes.
    Q_INVOKABLE int applyHeadingShortcut(QObject *quickDocument, int position);
    // Enter at the end of a heading starts a normal paragraph. Returns the new caret position, or -1.
    Q_INVOKABLE int endHeading(QObject *quickDocument, int position);
    Q_INVOKABLE void toggleList(QObject *quickDocument, int start, int end, bool numbered);
    Q_INVOKABLE int insertLink(QObject *quickDocument, int start, int end, const QString &url,
                              const QString &label, const QColor &color);
    Q_INVOKABLE int insertImage(QObject *quickDocument, int start, int end, const QString &url,
                               const QString &altText);
    Q_INVOKABLE void editCell(int table, int row, int col, const QString &text);
    // Removes a whole table from the source. Returns false when there is no such table.
    Q_INVOKABLE bool deleteTable(int tableIndex);
    Q_INVOKABLE void tableCommand(int table, const QString &command, int row, int col);
    // The insert functions return the index of the new table, or -1 when nothing was inserted.
    Q_INVOKABLE int insertTable(int block, int cursorUtf16, int rows, int cols);
    Q_INVOKABLE int insertTableAtVisualCursor(int block, int cursorPosition, int rows, int cols);
    Q_INVOKABLE int insertTableAtSourceCursor(int cursorUtf16, int rows, int cols);
    Q_INVOKABLE int insertTableAfterTable(int tableIndex, int rows, int cols);
    Q_INVOKABLE void setSourceText(const QString &text);
    Q_INVOKABLE void openLink(const QString &link);
    Q_INVOKABLE void copyLink(const QString &link);
    Q_INVOKABLE QPointF pointerPosition() const;
    Q_INVOKABLE bool checkDiskChanges();

signals:
    void tabsChanged();
    void blocksChanged();
    void projectFilesChanged();
    void recentFoldersChanged();
    void sourceChanged();
    void errorChanged();
    void unsupportedFileChanged();

private:
    OpenDocument *current();
    const OpenDocument *current() const;
    void rebuildBlocks();
    void rebuildProjectFiles();
    void setError(const QString &message);
    void clearUnsupportedFile();
    void changeSource(const QByteArray &newSource);
    int applyTableInsertion(const QByteArray &newSource);
    bool writeCurrent(const QString &path);
    QVector<OpenDocument> m_documents;
    int m_current = -1;
    QVariantList m_blocks;
    QVector<SourceBlock> m_sourceBlocks;
    QString m_projectFolder;
    QStringList m_recentFolders;
    bool m_rememberFolders = false;
    QVariantList m_projectFiles;
    QSet<QString> m_expandedFolders;
    QFileSystemWatcher m_folderWatcher;
    QString m_error;
    QString m_unsupportedFilePath;
    QString m_unsupportedFileMessage;
};
