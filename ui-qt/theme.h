#pragma once

#include <QColor>
#include <QDateTime>
#include <QFileSystemWatcher>
#include <QObject>
#include <QProcess>
#include <QString>
#include <QTimer>

class UiTheme : public QObject {
    Q_OBJECT
    Q_PROPERTY(QColor background READ background NOTIFY changed)
    Q_PROPERTY(QColor surface READ surface NOTIFY changed)
    Q_PROPERTY(QColor surfaceStrong READ surfaceStrong NOTIFY changed)
    Q_PROPERTY(QColor foreground READ foreground NOTIFY changed)
    Q_PROPERTY(QColor muted READ muted NOTIFY changed)
    Q_PROPERTY(QColor accent READ accent NOTIFY changed)
    Q_PROPERTY(QColor selection READ selection NOTIFY changed)
    Q_PROPERTY(QString monoFont READ monoFont NOTIFY changed)
    Q_PROPERTY(int zoomPercent READ zoomPercent NOTIFY zoomChanged)
public:
    explicit UiTheme(QObject *parent = nullptr);
    ~UiTheme() override;
    QColor background() const { return m_background; }
    QColor surface() const { return m_surface; }
    QColor surfaceStrong() const { return m_surfaceStrong; }
    QColor foreground() const { return m_foreground; }
    QColor muted() const { return m_muted; }
    QColor accent() const { return m_accent; }
    QColor selection() const { return m_selection; }
    QString monoFont() const { return m_monoFont; }
    int zoomPercent() const { return m_zoomPercent; }
    Q_INVOKABLE void zoomIn();
    Q_INVOKABLE void zoomOut();
    Q_INVOKABLE void resetZoom();
    Q_INVOKABLE void setZoomPercent(int percent);
    Q_INVOKABLE QString markdownHtml(const QString &source, const QColor &linkColor,
                                     const QString &fontFamily, int zoomPercent) const;

signals:
    void changed();
    void zoomChanged();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void reload();
    void watchThemeFiles();
    QColor m_background{QStringLiteral("#f4f1ea")};
    QColor m_surface{QStringLiteral("#e8e3d8")};
    QColor m_surfaceStrong{QStringLiteral("#d9d2c3")};
    QColor m_foreground{QStringLiteral("#2b2620")};
    QColor m_muted{QStringLiteral("#6f665a")};
    QColor m_accent{QStringLiteral("#9a4a24")};
    QColor m_selection{QStringLiteral("#d8c8a6")};
    QString m_monoFont{QStringLiteral("monospace")};
    QString m_themeIdentity;
    int m_zoomPercent = 100;
    int m_wheelAccumulated = 0;
    QFileSystemWatcher m_watcher;
    QTimer m_debounce;
    QProcess m_fontProcess;
};
