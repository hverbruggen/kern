#include "theme.h"
#include "htmlguard.h"

#include <QCoreApplication>
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QGuiApplication>
#include <QProcess>
#include <QPalette>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>
#include <QTextDocument>
#include <QTextBlock>
#include <QTextCursor>
#include <QWheelEvent>

UiTheme::UiTheme(QObject *parent) : QObject(parent)
{
    m_zoomPercent = qBound(30, QSettings(QStringLiteral("kern"), QStringLiteral("kern"))
        .value(QStringLiteral("ui/zoomPercent"), 100).toInt(), 200);
    QCoreApplication::instance()->installEventFilter(this);
    connect(&m_fontProcess, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
        [this](int exitCode, QProcess::ExitStatus status) {
            if (status != QProcess::NormalExit || exitCode != 0) return;
            const QString name = QString::fromUtf8(m_fontProcess.readAllStandardOutput()).trimmed();
            if (!name.isEmpty() && name != m_monoFont) {
                m_monoFont = name;
                emit changed();
            }
        });
    // Re-read the theme and font only when the files that define them change.
    m_debounce.setSingleShot(true);
    m_debounce.setInterval(300);
    connect(&m_debounce, &QTimer::timeout, this, &UiTheme::reload);
    const auto schedule = [this] { m_debounce.start(); };
    connect(&m_watcher, &QFileSystemWatcher::fileChanged, this, schedule);
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, schedule);
    reload();
}

void UiTheme::watchThemeFiles()
{
    // Theme switches replace files and directories, which drops their watches, so add them
    // again after every change.
    const QString state = QDir::home().filePath(QStringLiteral(".local/state/omarchy/current"));
    const QString fontconfig = QDir::home().filePath(QStringLiteral(".config/fontconfig"));
    const QStringList candidates{state, state + QStringLiteral("/theme"), state + QStringLiteral("/theme.name"),
                                 state + QStringLiteral("/theme/colors.toml"), fontconfig,
                                 fontconfig + QStringLiteral("/fonts.conf")};
    QStringList missing;
    for (const QString &path : candidates)
        if (QFileInfo::exists(path) && !m_watcher.files().contains(path) && !m_watcher.directories().contains(path))
            missing.push_back(path);
    if (!missing.isEmpty())
        m_watcher.addPaths(missing);
}

UiTheme::~UiTheme()
{
    if (m_fontProcess.state() != QProcess::NotRunning) {
        m_fontProcess.kill();
        m_fontProcess.waitForFinished(1000);
    }
}

void UiTheme::setZoomPercent(int percent)
{
    const int rounded = qBound(30, ((percent + 5) / 10) * 10, 200);
    if (rounded == m_zoomPercent)
        return;
    m_zoomPercent = rounded;
    QSettings(QStringLiteral("kern"), QStringLiteral("kern"))
        .setValue(QStringLiteral("ui/zoomPercent"), rounded);
    emit zoomChanged();
}

void UiTheme::zoomIn() { setZoomPercent(m_zoomPercent + 10); }
void UiTheme::zoomOut() { setZoomPercent(m_zoomPercent - 10); }
void UiTheme::resetZoom() { setZoomPercent(100); }

QString UiTheme::markdownHtml(const QString &source, const QColor &linkColor,
                              const QString &fontFamily, int zoomPercent) const
{
    QTextDocument document;
    // The document is built with a non-monospace default so that inline code keeps an explicit
    // monospace family, which the Markdown exporter needs to write backticks. The real font is
    // put back on the body afterwards.
    QFont font(QStringLiteral("Sans Serif"));
    font.setPixelSize(qRound(15.0 * zoomPercent / 100.0));
    document.setDefaultFont(font);
    document.setDefaultStyleSheet(QStringLiteral("a { color: %1; }").arg(linkColor.name()));
    QStringList tags;
    document.setMarkdown(htmlguard::protect(source, &tags));
    for (QTextBlock block = document.begin(); block.isValid(); block = block.next()) {
        QTextBlockFormat format = block.blockFormat();
        const QTextBlock next = block.next();
        const bool followedByListItem = block.textList() && next.isValid()
            && next.textList() == block.textList();
        format.setBottomMargin(followedByListItem ? 2 : format.headingLevel() > 0 ? 10 : 9);
        if (block.textList())
            format.setTopMargin(0);
        if (format.headingLevel() > 0)
            format.setTopMargin(block == document.begin() ? 4 : 16);
        QTextCursor cursor(block);
        cursor.setBlockFormat(format);
    }
    QString html = document.toHtml();
    html.replace(QStringLiteral("<body style=\" font-family:'Sans Serif';"),
                 QStringLiteral("<body style=\" font-family:'%1';").arg(fontFamily));
    html.replace(QRegularExpression(QStringLiteral("<a\\b")),
                 QStringLiteral("<a style=\"color:%1\"").arg(linkColor.name()));
    html.replace(QStringLiteral("color:#0000ff;"),
                 QStringLiteral("color:%1;").arg(linkColor.name()));
    return html;
}

bool UiTheme::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::Wheel) {
        auto *wheel = static_cast<QWheelEvent *>(event);
        if (wheel->modifiers().testFlag(Qt::ControlModifier)) {
            const int delta = wheel->angleDelta().y() != 0 ? wheel->angleDelta().y() : wheel->pixelDelta().y() * 4;
            if (delta != 0) {
                m_wheelAccumulated += delta;
                while (m_wheelAccumulated >= 120) { zoomIn(); m_wheelAccumulated -= 120; }
                while (m_wheelAccumulated <= -120) { zoomOut(); m_wheelAccumulated += 120; }
                wheel->accept();
                return true;
            }
        }
    }
    return QObject::eventFilter(watched, event);
}

void UiTheme::reload()
{
    watchThemeFiles();
    const QString path = QDir::home().filePath(QStringLiteral(".local/state/omarchy/current/theme/colors.toml"));
    const QFileInfo info(path);
    const QString identity = info.exists()
        ? info.canonicalFilePath() + QString::number(info.lastModified().toMSecsSinceEpoch())
        : QStringLiteral("default");
    if (identity != m_themeIdentity) {
        m_themeIdentity = identity;
        QColor background(QStringLiteral("#f4f1ea"));
        QColor surface(QStringLiteral("#e8e3d8"));
        QColor strong(QStringLiteral("#d9d2c3"));
        QColor foreground(QStringLiteral("#2b2620"));
        QColor muted(QStringLiteral("#6f665a"));
        QColor accent(QStringLiteral("#9a4a24"));
        QColor selection(QStringLiteral("#d8c8a6"));
        QFile file(path);
        if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            const QRegularExpression entry(QStringLiteral(R"pattern(^\s*([a-z_]+)\s*=\s*"(#[0-9a-fA-F]{6})")pattern"));
            for (const QByteArray &line : file.readAll().split('\n')) {
                const auto match = entry.match(QString::fromUtf8(line));
                if (!match.hasMatch()) continue;
                const QString key = match.captured(1);
                const QColor color(match.captured(2));
                if (key == QStringLiteral("background")) background = color;
                else if (key == QStringLiteral("dark_background")) surface = color;
                else if (key == QStringLiteral("darker_background")) strong = color;
                else if (key == QStringLiteral("foreground")) foreground = color;
                else if (key == QStringLiteral("dark_foreground")) muted = color;
                else if (key == QStringLiteral("accent")) accent = color;
                else if (key == QStringLiteral("selection")) selection = color;
            }
        }
        m_background = background;
        m_surface = surface;
        m_surfaceStrong = strong;
        m_foreground = foreground;
        m_muted = muted;
        m_accent = accent;
        m_selection = selection;
        QPalette palette;
        palette.setColor(QPalette::Window, background);
        palette.setColor(QPalette::Base, background);
        palette.setColor(QPalette::AlternateBase, surface);
        palette.setColor(QPalette::Button, surface);
        palette.setColor(QPalette::Light, surface);
        palette.setColor(QPalette::Midlight, surface);
        palette.setColor(QPalette::Mid, strong);
        palette.setColor(QPalette::Dark, strong);
        palette.setColor(QPalette::Shadow, foreground);
        palette.setColor(QPalette::Text, foreground);
        palette.setColor(QPalette::WindowText, foreground);
        palette.setColor(QPalette::ButtonText, foreground);
        palette.setColor(QPalette::BrightText, foreground);
        palette.setColor(QPalette::PlaceholderText, muted);
        palette.setColor(QPalette::Highlight, selection);
        palette.setColor(QPalette::HighlightedText, foreground);
        palette.setColor(QPalette::Link, accent);
        palette.setColor(QPalette::LinkVisited, accent);
        palette.setColor(QPalette::ToolTipBase, surface);
        palette.setColor(QPalette::ToolTipText, foreground);
        for (auto role : {QPalette::Text, QPalette::WindowText, QPalette::ButtonText})
            palette.setColor(QPalette::Disabled, role, muted);
        QGuiApplication::setPalette(palette);
        emit changed();
    }

    const QString omarchy = QStandardPaths::findExecutable(QStringLiteral("omarchy"));
    if (!omarchy.isEmpty() && m_fontProcess.state() == QProcess::NotRunning)
        m_fontProcess.start(omarchy, {QStringLiteral("font"), QStringLiteral("current")});
}
