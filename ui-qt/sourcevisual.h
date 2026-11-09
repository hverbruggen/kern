#pragma once
#include <QSyntaxHighlighter>
#include <QJsonArray>
#include <QVariantList>
#include <QUrl>
#include <QCache>
#include <QTextDocument>
#include <QTextObjectInterface>
#include "mathrenderer.h"

QString documentSource(const QTextDocument *doc);

// A reversible, single-character space at a Markdown soft break. Qt retains
// the object format in its native undo stack; source positions remain 1:1.
class SoftBreakSpace final : public QObject, public QTextObjectInterface {
    Q_OBJECT
    Q_INTERFACES(QTextObjectInterface)
public:
    using QObject::QObject;
    static constexpr int Type = QTextFormat::UserObject + 1;
    static constexpr int OriginalCharacter = QTextFormat::UserProperty + 1;
    QSizeF intrinsicSize(QTextDocument *, int, const QTextFormat &) override;
    void drawObject(QPainter *, const QRectF &, QTextDocument *, int, const QTextFormat &) override {}
};

// Source-preserving presentation. Markers use layout formats; soft breaks use
// reversible inline spaces with identical source offsets and native undo metadata.
class SourceVisual final : public QSyntaxHighlighter {
    Q_OBJECT
    Q_PROPERTY(QVariantList decorations READ decorations NOTIFY updated)
public:
    explicit SourceVisual(QTextDocument *document);
    QVariantList decorations() const { return widgets; }
    Q_INVOKABLE void configure(QColor foreground, QColor accent, QColor muted, QColor background,
                               QString family, int size, QUrl baseUrl, qreal width);
    Q_INVOKABLE void select(int start, int end, bool focused);
    Q_INVOKABLE void setPreview(bool enabled) { preview=enabled; parse(); }
    Q_INVOKABLE void activate(int start, int end, const QString &kind);
    Q_INVOKABLE QString linkAt(int position) const;
    Q_INVOKABLE bool hiddenAt(int position) const;
    Q_INVOKABLE bool literalAt(int position) const;
    Q_INVOKABLE void copySource(int start, int end, bool cut = false);
    Q_INVOKABLE void watchEditor(QObject *editor) { editor->installEventFilter(this); }
    static SourceVisual *get(QTextDocument *document);
signals:
    void updated();
    void projectionStarting();
    void projectionFinished();
private:
    void highlightBlock(const QString &text) override;
    bool eventFilter(QObject *watched, QEvent *event) override;
    void parse();
    void refresh();
    void projectSoftBreaks();
    void merge(int from, int to, const QTextCharFormat &format);
    bool touches(int from, int to) const;
    QString source, family;
    QJsonArray nodes;
    QVariantList widgets;
    QVector<QPair<int,int>> hidden;
    struct Space { int from; int width; int height; };
    QVector<Space> spaces;
    QCache<QString, EquationImage> equationCache{128};
    QColor foreground, accent, muted, background;
    QUrl baseUrl;
    int size=15, start=-1, end=-1;
    qreal availableWidth=600;
    bool refreshing=false, queued=false, preview=true, projecting=false, dispatching=false;
};
