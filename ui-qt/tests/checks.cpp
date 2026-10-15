#include "checks.h"
#include "../controller.h"
#include "../theme.h"

#include <QGuiApplication>
#include <QClipboard>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QSettings>
#include <QFile>
#include <QFont>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlProperty>
#include <QQuickWindow>
#include <QQuickItem>
#include <QQuickTextDocument>
#include <QTextDocument>
#include <QTemporaryDir>
#include <QTimer>
#include <QWheelEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QEventLoop>
#include <QRegularExpression>
#include <QPalette>

static QQuickItem *findVisual(QQuickItem *item, const QString &name)
{
    if (!item)
        return nullptr;
    if (item->objectName() == name)
        return item;
    for (QQuickItem *child : item->childItems())
        if (auto *found = findVisual(child, name))
            return found;
    return nullptr;
}

int runSelfTest()
{
    QFile fixture(QStringLiteral(KERN_QT_SOURCE_DIR "/tests/fixtures/representative.md"));
    if (!fixture.open(QIODevice::ReadOnly))
        return 10;
    const QByteArray original = fixture.readAll();
    QTemporaryDir folder;
    if (!folder.isValid())
        return 11;
    const QString path = folder.filePath(QStringLiteral("sample.md"));
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(original) != original.size())
        return 12;
    file.close();

    Controller controller;
    if (!controller.openPath(path) || controller.sourceText().toUtf8() != original)
        return 13;
    controller.editCell(0, 1, 0, QStringLiteral("Changed"));
    const QByteArray edited = controller.sourceText().toUtf8();
    QByteArray expected = original;
    expected.replace("| Alpha | 12 | *review* |", "| Changed | 12 | *review* |");
    if (edited != expected)
        return 14;
    controller.undo();
    if (controller.sourceText().toUtf8() != original)
        return 15;
    controller.redo();
    if (controller.sourceText().toUtf8() != expected)
        return 16;
    if (!controller.save())
        return 17;
    QFile saved(path);
    if (!saved.open(QIODevice::ReadOnly) || saved.readAll() != expected)
        return 18;
    controller.tableCommand(0, QStringLiteral("row-below"), 1, 0);
    if (!controller.sourceText().contains(QStringLiteral("|  |  |  |")))
        return 19;
    controller.undo();
    if (controller.sourceText().toUtf8() != expected)
        return 20;

    QFile external(path);
    if (!external.open(QIODevice::WriteOnly | QIODevice::Truncate)
        || external.write(original) != original.size())
        return 26;
    external.close();
    if (!external.open(QIODevice::ReadWrite)
        || !external.setFileTime(QDateTime::currentDateTime().addSecs(5), QFileDevice::FileModificationTime))
        return 27;
    external.close();
    if (!controller.checkDiskChanges())
        return 28;
    if (controller.save())
        return 30;
    if (!controller.reloadCurrent(true) || controller.sourceText().toUtf8() != original)
        return 29;

    Controller newDocument;
    newDocument.newDocument();
    newDocument.insertTable(0, 0, 3, 2);
    if (!newDocument.sourceText().contains(QStringLiteral("|  |  |\n| --- | --- |")))
        return 21;

    Controller insertion;
    const QString insertionPath = folder.filePath(QStringLiteral("insert.md"));
    QFile insertionFile(insertionPath);
    const QByteArray insertionSource("Before **bold** tail.\n\nAfter paragraph.\n");
    if (!insertionFile.open(QIODevice::WriteOnly)
        || insertionFile.write(insertionSource) != insertionSource.size())
        return 31;
    insertionFile.close();
    if (!insertion.openPath(insertionPath))
        return 32;
    QTextDocument insertionText;
    insertionText.setMarkdown(QString::fromUtf8(insertionSource));
    const int visualCursor = insertionText.toPlainText().indexOf(QStringLiteral("tail"));
    if (visualCursor < 0)
        return 33;
    insertion.insertTableAtVisualCursor(0, visualCursor, 2, 2);
    const QString insertedSource = insertion.sourceText();
    const int beforePosition = insertedSource.indexOf(QStringLiteral("Before **bold**"));
    const int tablePosition = insertedSource.indexOf(QStringLiteral("|  |  |\n| --- | --- |"));
    const int afterPosition = insertedSource.indexOf(QStringLiteral("tail."));
    if (beforePosition < 0 || tablePosition <= beforePosition || afterPosition <= tablePosition
        || !insertedSource.contains(QStringLiteral("After paragraph.")))
        return 34;

    // History is stored as deltas: many edits, then undo and redo all the way.
    {
        Controller history;
        history.newDocument();
        const QStringList states{QStringLiteral(""), QStringLiteral("a"), QStringLiteral("ab"),
                                 QStringLiteral("a\u00e9b"), QStringLiteral("\u00e9b")};
        for (int i = 1; i < states.size(); ++i)
            history.setSourceText(states[i]);
        for (int i = states.size() - 2; i >= 0; --i) {
            history.undo();
            if (history.sourceText() != states[i]) return 60;
        }
        for (int i = 1; i < states.size(); ++i) {
            history.redo();
            if (history.sourceText() != states[i]) return 61;
        }
    }

    // Editing one paragraph in Visual mode must not rewrite the others.
    {
        Controller visual;
        const QString visualPath = folder.filePath(QStringLiteral("visual.md"));
        const QByteArray visualSource("Keep \\* this  \nhard break and <b>html</b>.\n\nSecond paragraph.\n\n[ref][1]\n\n[1]: http://example.org\n");
        QFile visualFile(visualPath);
        if (!visualFile.open(QIODevice::WriteOnly) || visualFile.write(visualSource) != visualSource.size())
            return 62;
        visualFile.close();
        if (!visual.openPath(visualPath)) return 63;
        UiTheme visualTheme;
        QString html = visualTheme.markdownHtml(QString::fromUtf8(visualSource), Qt::blue,
                                                QStringLiteral("monospace"), 100);
        if (!html.contains(QStringLiteral("Second paragraph."))) return 64;
        html.replace(QStringLiteral("Second paragraph."), QStringLiteral("Second changed."));
        visual.editFormattedBlock(0, html);
        QByteArray expectedVisual = visualSource;
        expectedVisual.replace("Second paragraph.", "Second changed.");
        if (!visual.sourceText().startsWith(QString::fromUtf8(expectedVisual.left(expectedVisual.indexOf("Second")))))
            return 65;
        if (!visual.sourceText().contains(QStringLiteral("Second changed.")) || visual.sourceText().contains(QStringLiteral("Second paragraph.")))
            return 66;
    }

    // Inline HTML in the paragraph being edited survives, in text blocks and in table cells.
    {
        Controller guarded;
        const QString guardedPath = folder.filePath(QStringLiteral("guarded.md"));
        const QByteArray guardedSource("Plain <span style=\"color:red\">red</span> word and `<b>code</b>`.\n\n| A | B |\n| --- | --- |\n| x<br>y | <u>z</u> |\n");
        QFile guardedFile(guardedPath);
        if (!guardedFile.open(QIODevice::WriteOnly) || guardedFile.write(guardedSource) != guardedSource.size())
            return 77;
        guardedFile.close();
        if (!guarded.openPath(guardedPath)) return 78;
        UiTheme guardedTheme;
        const QVariantMap textBlock = guarded.blocks()[0].toMap();
        QString html = guardedTheme.markdownHtml(textBlock.value(QStringLiteral("preview")).toString(), Qt::blue,
                                                 QStringLiteral("monospace"), 100);
        if (!html.contains(QStringLiteral("word and"))) return 79;
        html.replace(QStringLiteral("word and"), QStringLiteral("term and"));
        guarded.editFormattedBlock(0, html);
        if (!guarded.sourceText().startsWith(QStringLiteral("Plain <span style=\"color:red\">red</span> term and `<b>code</b>`.")))
            return 80;
        if (!html.contains(QStringLiteral("font-family:'monospace'"))) return 82;
        const QVariantMap tableBlock = guarded.blocks()[1].toMap();
        const QString cell = tableBlock.value(QStringLiteral("rows")).toList()[1].toList()[0].toString();
        QString cellHtml = guardedTheme.markdownHtml(cell, Qt::blue, QStringLiteral("monospace"), 100);
        cellHtml.replace(QStringLiteral(">x"), QStringLiteral(">w"));
        guarded.editFormattedCell(0, 1, 0, cellHtml);
        if (!guarded.sourceText().contains(QStringLiteral("| w<br>y | <u>z</u> |"))) return 81;
    }

    // Links: percent-encoding is decoded, and unsafe or missing targets are refused.
    {
        Controller links;
        links.newDocument();
        links.openLink(QStringLiteral("javascript:alert(1)"));
        if (links.error().isEmpty()) return 67;
        Controller located;
        const QString linkPath = folder.filePath(QStringLiteral("links.md"));
        QFile linkFile(linkPath);
        if (!linkFile.open(QIODevice::WriteOnly) || linkFile.write("x\n") != 2) return 68;
        linkFile.close();
        if (!located.openPath(linkPath)) return 69;
        located.openLink(QStringLiteral("no%20such%20file.md#section"));
        if (!located.error().contains(QStringLiteral("no such file.md"))) return 70;
        located.openLink(QStringLiteral("smb://host/share"));
        if (!located.error().contains(QStringLiteral("smb"))) return 71;
    }

    // Project tree: symlink loops terminate and an oversized folder shows a truncation entry.
    {
        QTemporaryDir tree;
        if (!tree.isValid()) return 72;
        QDir(tree.path()).mkpath(QStringLiteral("a"));
        if (!QFile::link(tree.path(), tree.filePath(QStringLiteral("a/loop")))) return 73;
        QDir(tree.path()).mkpath(QStringLiteral("big"));
        for (int i = 0; i < 2100; ++i) {
            QFile entry(tree.filePath(QStringLiteral("big/f%1.txt").arg(i, 4, 10, QLatin1Char('0'))));
            if (!entry.open(QIODevice::WriteOnly)) return 74;
        }
        Controller project;
        project.setProjectFolder(tree.path());
        for (int round = 0; round < 6; ++round) {
            const QVariantList files = project.projectFiles();
            int target = -1;
            for (int i = 0; i < files.size(); ++i) {
                const QVariantMap entry = files[i].toMap();
                if (entry.value(QStringLiteral("directory")).toBool() && !entry.value(QStringLiteral("expanded")).toBool()
                    && entry.value(QStringLiteral("name")).toString() != QStringLiteral("big")) {
                    target = i;
                    break;
                }
            }
            if (target < 0) break;
            project.activateProjectEntry(target);
        }
        if (project.projectFiles().size() > 2100) return 75;
        const QVariantList files = project.projectFiles();
        for (int i = 0; i < files.size(); ++i)
            if (files[i].toMap().value(QStringLiteral("name")).toString() == QStringLiteral("big"))
                project.activateProjectEntry(i);
        const QVariantMap last = project.projectFiles().last().toMap();
        if (!last.value(QStringLiteral("info")).toBool()) return 76;
    }

    // Project tree: files added on disk show up without any user action, in the root and in an expanded folder.
    {
        QTemporaryDir live;
        if (!live.isValid()) return 77;
        QDir(live.path()).mkpath(QStringLiteral("sub"));
        Controller project;
        project.setProjectFolder(live.path());
        const auto listed = [&project](const QString &name) {
            for (const QVariant &entry : project.projectFiles())
                if (entry.toMap().value(QStringLiteral("name")).toString() == name) return true;
            return false;
        };
        const auto waitFor = [&](const QString &name) {
            QElapsedTimer clock;
            clock.start();
            while (!listed(name) && clock.elapsed() < 1000)
                QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
            return listed(name);
        };
        for (int i = 0; i < project.projectFiles().size(); ++i)
            if (project.projectFiles()[i].toMap().value(QStringLiteral("name")).toString() == QStringLiteral("sub")) {
                project.activateProjectEntry(i);
                break;
            }
        QFile rootFile(live.filePath(QStringLiteral("new-root.md")));
        if (!rootFile.open(QIODevice::WriteOnly)) return 78;
        rootFile.close();
        if (!waitFor(QStringLiteral("new-root.md"))) return 79;
        QFile subFile(live.filePath(QStringLiteral("sub/new-sub.md")));
        if (!subFile.open(QIODevice::WriteOnly)) return 80;
        subFile.close();
        if (!waitFor(QStringLiteral("new-sub.md"))) return 81;
    }

    for (const QString &name : {QStringLiteral("formatting.md"), QStringLiteral("escaped.md")}) {
        QFile input(QStringLiteral(KERN_QT_SOURCE_DIR "/tests/fixtures/") + name);
        if (!input.open(QIODevice::ReadOnly))
            return 22;
        const QByteArray bytes = input.readAll();
        const QString copyPath = folder.filePath(name);
        QFile copy(copyPath);
        if (!copy.open(QIODevice::WriteOnly) || copy.write(bytes) != bytes.size())
            return 23;
        copy.close();
        Controller roundTrip;
        if (!roundTrip.openPath(copyPath) || !roundTrip.save())
            return 24;
        QFile after(copyPath);
        if (!after.open(QIODevice::ReadOnly) || after.readAll() != bytes)
            return 25;
    }
    return 0;
}

int runChecks(QGuiApplication &app, QQmlApplicationEngine &engine, Controller &controller, UiTheme &theme,
              const QStringList &arguments, const QList<QQmlError> &qmlWarnings)
{
    if (arguments.contains(QStringLiteral("--zoom-check"))) {
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        QQuickItem *preview = window ? findVisual(window->contentItem(), QStringLiteral("formatted-editor-0")) : nullptr;
        if (!preview) return 50;
        const int savedZoom = theme.zoomPercent();
        theme.setZoomPercent(100);
        const int before = preview->property("font").value<QFont>().pixelSize();
        QWheelEvent wheel(QPointF(100, 100), QPointF(100, 100), QPoint(), QPoint(0, 120),
                          Qt::NoButton, Qt::ControlModifier, Qt::NoScrollPhase, false);
        QCoreApplication::sendEvent(window, &wheel);
        const int after = preview->property("font").value<QFont>().pixelSize();
        theme.setZoomPercent(30);
        theme.zoomOut();
        const int minimumSize = preview->property("font").value<QFont>().pixelSize();
        const bool works = theme.zoomPercent() == 30 && minimumSize < before && after > before;
        theme.setZoomPercent(savedZoom);
        return works ? 0 : 51;
    }
    if (arguments.contains(QStringLiteral("--dialogs-check"))) {
        QObject *root = engine.rootObjects().first();
        const QPalette palette = QGuiApplication::palette();
        for (auto role : {QPalette::Button, QPalette::Light, QPalette::Midlight, QPalette::ToolTipBase})
            if (palette.color(role) != theme.surface()) return 212;
        for (auto role : {QPalette::Window, QPalette::Base})
            if (palette.color(role) != theme.background()) return 213;
        for (auto role : {QPalette::Text, QPalette::WindowText, QPalette::ButtonText, QPalette::ToolTipText})
            if (palette.color(role) != theme.foreground()) return 214;
        for (const QString &name : {QStringLiteral("save-file-dialog"),
                                    QStringLiteral("new-file-dialog"), QStringLiteral("folder-dialog"),
                                    QStringLiteral("image-file-dialog"), QStringLiteral("discard-dialog"),
                                    QStringLiteral("rich-insert-dialog"), QStringLiteral("new-table-dialog"),
                                    QStringLiteral("error-dialog")}) {
            QObject *dialog = root->findChild<QObject *>(name);
            if (!dialog || !QMetaObject::invokeMethod(dialog, "open"))
                return 40;
            QEventLoop wait;
            QTimer::singleShot(100, &wait, &QEventLoop::quit);
            wait.exec();
            // Check the palettes used by actual Quick controls, including separate popup windows.
            const auto checkPalette = [&](auto &&self, QQuickItem *item) -> bool {
                if (item->metaObject()->indexOfProperty("palette") >= 0
                    && QQmlProperty::read(item, QStringLiteral("palette.light")).value<QColor>() != theme.surface())
                    return false;
                for (auto *child : item->childItems())
                    if (!self(self, child)) return false;
                return true;
            };
            for (auto *candidate : QGuiApplication::allWindows()) {
                auto *quick = qobject_cast<QQuickWindow *>(candidate);
                if (quick && quick->isVisible() && !checkPalette(checkPalette, quick->contentItem())) return 216;
            }
            const int captureIndex = arguments.indexOf(QStringLiteral("--screenshot"));
            if (captureIndex >= 0 && captureIndex + 1 < arguments.size()) {
                for (auto *candidate : QGuiApplication::allWindows()) {
                    auto *quick = qobject_cast<QQuickWindow *>(candidate);
                    if (quick && quick->isVisible())
                        quick->grabWindow().save(arguments[captureIndex + 1] + "." + name
                            + (quick == qobject_cast<QQuickWindow *>(root) ? ".main.png" : ".popup.png"));
                }
            }
            QMetaObject::invokeMethod(dialog, "close");
        }
        return qmlWarnings.isEmpty() ? 0 : 215;
    }
    if (arguments.contains(QStringLiteral("--dialog-keys-check"))) {
        QObject *root = engine.rootObjects().first();
        auto *window = qobject_cast<QQuickWindow *>(root);
        QObject *dialog = root->findChild<QObject *>(QStringLiteral("discard-dialog"));
        if (!window || !dialog) return 260;
        window->requestActivate();
        auto settle = [] {
            QEventLoop wait;
            QTimer::singleShot(150, &wait, &QEventLoop::quit);
            wait.exec();
        };
        auto press = [&](Qt::Key key) {
            QKeyEvent down(QEvent::KeyPress, key, Qt::NoModifier);
            QCoreApplication::sendEvent(window, &down);
            QKeyEvent up(QEvent::KeyRelease, key, Qt::NoModifier);
            QCoreApplication::sendEvent(window, &up);
            settle();
        };
        auto button = [&](const char *name) -> QQuickItem * {
            for (auto *candidate : root->findChildren<QQuickItem *>(QString::fromLatin1(name)))
                if (candidate->isVisible() && candidate->window() == window)
                    return candidate;
            return nullptr;
        };
        // Right then Enter cancels; the dialog closes and focus returns to the window.
        QMetaObject::invokeMethod(dialog, "open");
        settle();
        QQuickItem *accept = button("dialog-accept");
        if (!accept || !accept->hasActiveFocus()) return 261;
        press(Qt::Key_Right);
        QQuickItem *reject = button("dialog-reject");
        if (!reject || !reject->hasActiveFocus() || accept->hasActiveFocus()) return 262;
        press(Qt::Key_Left);
        if (!accept->hasActiveFocus()) return 263;
        press(Qt::Key_Right);
        press(Qt::Key_Return);
        if (dialog->property("visible").toBool()) return 264;
        // Enter alone takes the default action (Discard) and closes the dialog.
        QMetaObject::invokeMethod(dialog, "open");
        settle();
        if (!button("dialog-accept")->hasActiveFocus()) return 265;
        press(Qt::Key_Enter);
        if (dialog->property("visible").toBool()) return 266;
        return 0;
    }
    if (arguments.contains(QStringLiteral("--blank-area-check"))) {
        auto *root = engine.rootObjects().first();
        auto *window = qobject_cast<QQuickWindow *>(root);
        QTemporaryDir folder;
        if (!window || !folder.isValid()) return 234;
        window->requestActivate();
        auto settle = [] {
            QEventLoop wait;
            QTimer::singleShot(50, &wait, &QEventLoop::quit);
            wait.exec();
        };
        auto click = [&](QQuickItem *item, const QPointF &point) {
            const QPointF scene = item->mapToScene(point);
            const QPointF global = window->mapToGlobal(scene.toPoint());
            QMouseEvent down(QEvent::MouseButtonPress, scene, scene, global,
                             Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
            QCoreApplication::sendEvent(window, &down);
            QMouseEvent up(QEvent::MouseButtonRelease, scene, scene, global,
                           Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
            QCoreApplication::sendEvent(window, &up);
            settle();
        };
        int fileIndex = 0;
        for (bool source : {false, true}) {
            for (const QString &content : {QString(), QStringLiteral("Short document"),
                    QStringLiteral("Before\n\n| Column | Other |\n| --- | --- |\n| Value | Cell |")}) {
                const QString path = folder.filePath(QString::number(fileIndex++) + (source ? ".txt" : ".md"));
                QFile file(path);
                if (!file.open(QIODevice::WriteOnly) || file.write(content.toUtf8()) != content.toUtf8().size())
                    return 235;
                file.close();
                if (!controller.openPath(path)) return 236;
                root->setProperty("sourceMode", source);
                settle();
                auto *target = root->findChild<QQuickItem *>(source ? QStringLiteral("source-editor")
                    : QStringLiteral("document-end-click-area"));
                if (!target || target->height() < 100) return 237;
                click(target, QPointF(target->width() / 2, target->height() - 35));
                auto *active = window->activeFocusItem();
                if (!active || active->property("readOnly").toBool()
                    || active->property("cursorPosition").toInt() != active->property("length").toInt()) return 238;
                QKeyEvent letter(QEvent::KeyPress, Qt::Key_X, Qt::NoModifier, QStringLiteral("X"));
                QCoreApplication::sendEvent(window, &letter);
                if (!QMetaObject::invokeMethod(root, "commitActive")) return 239;
                if (!controller.sourceText().endsWith(QStringLiteral("X"))) return 240;
                if (!content.contains(QStringLiteral("| Column |"))) {
                    if (controller.sourceText() != content + QStringLiteral("X")) return 241;
                } else if (!source && !controller.sourceText().contains(QStringLiteral("| Value | Cell |\n\nX"))) { fprintf(stderr, "TRAILING TABLE SOURCE: %s\n", qPrintable(controller.sourceText())); return 242; }
            }
        }
        // Plain text also uses the visual block editor by default.
        root->setProperty("sourceMode", false);
        settle();
        auto *target = root->findChild<QQuickItem *>(QStringLiteral("document-end-click-area"));
        click(target, QPointF(target->width() / 2, target->height() - 35));
        QKeyEvent letter(QEvent::KeyPress, Qt::Key_Y, Qt::NoModifier, QStringLiteral("Y"));
        QCoreApplication::sendEvent(window, &letter);
        if (!QMetaObject::invokeMethod(root, "commitActive") || !controller.sourceText().endsWith("XY")) return 243;
        for (const auto &warning : qmlWarnings) fprintf(stderr, "%s\n", qPrintable(warning.toString()));
        return qmlWarnings.isEmpty() ? 0 : 244;
    }
    if (arguments.contains(QStringLiteral("--unsupported-file-check"))) {
        auto *root = engine.rootObjects().first();
        auto *window = qobject_cast<QQuickWindow *>(root);
        QTemporaryDir folder;
        if (!window || !folder.isValid()) return 220;
        auto write = [&](const QString &name, const QByteArray &data) {
            QFile file(folder.filePath(name));
            return file.open(QIODevice::WriteOnly) && file.write(data) == data.size();
        };
        if (!write(QStringLiteral("binary.bin"), QByteArray("A\0B", 3))
            || !write(QStringLiteral("legacy.txt"), QByteArray::fromHex("636166e9"))
            || !write(QStringLiteral("valid.txt"), QByteArray("Editable"))) return 221;
        controller.setProjectFolder(folder.path());
        window->requestActivate();
        auto settle = [] {
            QEventLoop wait;
            QTimer::singleShot(50, &wait, &QEventLoop::quit);
            wait.exec();
        };
        auto clickFile = [&](const QString &name) {
            settle();
            auto *item = findVisual(window->contentItem(), QStringLiteral("project-entry-") + name);
            if (!item || !QMetaObject::invokeMethod(item, "clicked")) return false;
            settle();
            return true;
        };
        auto *overlay = root->findChild<QQuickItem *>(QStringLiteral("unsupported-file-overlay"));
        auto *error = root->findChild<QObject *>(QStringLiteral("error-dialog"));
        if (!overlay || !error || !clickFile(QStringLiteral("binary.bin"))) return 222;
        if (!overlay->isVisible() || error->property("visible").toBool() || controller.hasDocument()
            || controller.unsupportedFileMessage().isEmpty()) return 223;
        if (!clickFile(QStringLiteral("valid.txt")) || overlay->isVisible()
            || controller.sourceText() != QStringLiteral("Editable")) return 224;
        auto *editor = findVisual(window->contentItem(), QStringLiteral("formatted-editor-0"));
        if (!editor) return 225;
        editor->forceActiveFocus();
        editor->setProperty("cursorPosition", 8);
        QKeyEvent key(QEvent::KeyPress, Qt::Key_X, Qt::NoModifier, QStringLiteral("X"));
        QCoreApplication::sendEvent(window, &key);
        if (!clickFile(QStringLiteral("legacy.txt")) || !overlay->isVisible()
            || error->property("visible").toBool() || controller.sourceText() != QStringLiteral("EditableX")) return 226;
        const int captureIndex = arguments.indexOf(QStringLiteral("--screenshot"));
        if (captureIndex >= 0 && captureIndex + 1 < arguments.size())
            window->grabWindow().save(arguments[captureIndex + 1]);
        QCoreApplication::sendEvent(window, &key);
        if (controller.sourceText() != QStringLiteral("EditableX")) return 227;
        // Selecting the already active tab must clear the message too.
        auto *tab = findVisual(window->contentItem(), QStringLiteral("document-tab-0"));
        if (!tab || !QMetaObject::invokeMethod(tab, "clicked")) return 228;
        settle();
        if (overlay->isVisible() || !controller.unsupportedFileMessage().isEmpty()
            || controller.sourceText() != QStringLiteral("EditableX")) return 229;
        controller.newDocument();
        if (!clickFile(QStringLiteral("binary.bin")) || !overlay->isVisible()
            || !clickFile(QStringLiteral("valid.txt")) || overlay->isVisible()
            || controller.sourceText() != QStringLiteral("EditableX")) return 230;
        if (!clickFile(QStringLiteral("binary.bin")) || !overlay->isVisible()) return 231;
        controller.newDocument();
        settle();
        if (overlay->isVisible() || error->property("visible").toBool()) return 232;
        return qmlWarnings.isEmpty() ? 0 : 233;
    }
    if (arguments.contains(QStringLiteral("--file-flow-check"))) {
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        QObject *root = engine.rootObjects().first();
        QTemporaryDir folder;
        if (!window || !folder.isValid()) return 180;
        controller.setProjectFolder(folder.path());
        window->requestActivate();
        auto settle = [] {
            QEventLoop wait;
            QTimer::singleShot(200, &wait, &QEventLoop::quit);
            wait.exec();
        };
        QObject *dialog = root->findChild<QObject *>(QStringLiteral("new-file-dialog"));
        if (!dialog || !QMetaObject::invokeMethod(dialog, "open")) return 181;
        settle();
        auto *filename = root->findChild<QQuickItem *>(QStringLiteral("new-file-name"));
        QObject *create = nullptr;
        for (auto *button : root->findChildren<QObject *>(QStringLiteral("dialog-accept")))
            if (button->property("text").toString() == QStringLiteral("Create")) create = button;
        if (!filename || !filename->hasActiveFocus() || !create
            || create->property("text").toString() != QStringLiteral("Create")) return 182;
        auto pressEnter = [&](Qt::Key key) {
            QKeyEvent down(QEvent::KeyPress, key, Qt::NoModifier);
            QCoreApplication::sendEvent(window, &down);
            QKeyEvent up(QEvent::KeyRelease, key, Qt::NoModifier);
            QCoreApplication::sendEvent(window, &up);
            settle();
        };
        // Exercise real keyboard submission, including rejected names and both Enter keys.
        for (const QString &invalid : {QString(), QStringLiteral("   "), QStringLiteral("."),
                                      QStringLiteral(".."), QStringLiteral("sub/file.txt")}) {
            filename->setProperty("text", invalid);
            pressEnter(Qt::Key_Return);
            if (!dialog->property("visible").toBool() || !filename->hasActiveFocus()
                || dialog->property("pathError").toString().isEmpty() || controller.hasDocument()) return 205;
        }
        QFile existing(folder.filePath(QStringLiteral("existing.txt")));
        if (!existing.open(QIODevice::WriteOnly) || existing.write("keep") != 4) return 206;
        existing.close();
        filename->setProperty("text", QStringLiteral("existing.txt"));
        if (!QMetaObject::invokeMethod(create, "clicked")) return 207;
        settle();
        if (!dialog->property("visible").toBool() || dialog->property("pathError").toString().isEmpty()
            || !existing.open(QIODevice::ReadOnly) || existing.readAll() != QByteArray("keep")) return 208;
        existing.close();
        const int captureIndex = arguments.indexOf(QStringLiteral("--screenshot"));
        if (captureIndex >= 0 && captureIndex + 1 < arguments.size())
            window->grabWindow().save(arguments[captureIndex + 1] + QStringLiteral(".new-file.png"));
        filename->setProperty("text", QStringLiteral("new.txt"));
        pressEnter(Qt::Key_Return);
        if (!QFileInfo::exists(folder.filePath(QStringLiteral("new.txt")))) return 183;
        settle();
        auto *editor = findVisual(window->contentItem(), QStringLiteral("formatted-editor-0"));
        if (!editor || !editor->hasActiveFocus() || controller.markdown()) return 184;
        QKeyEvent letter(QEvent::KeyPress, Qt::Key_A, Qt::NoModifier, QStringLiteral("A"));
        QCoreApplication::sendEvent(window, &letter);
        if (!QMetaObject::invokeMethod(root, "commitActive") || controller.sourceText() != QStringLiteral("A"))
            return 185;
        settle();
        editor = findVisual(window->contentItem(), QStringLiteral("formatted-editor-0"));
        editor->forceActiveFocus();
        if (!QMetaObject::invokeMethod(root, "openInsertDialog", Q_ARG(QVariant, QVariant(QStringLiteral("image")))))
            return 186;
        settle();
        auto *richDialog = root->findChild<QObject *>(QStringLiteral("rich-insert-dialog"));
        auto *url = root->findChild<QQuickItem *>(QStringLiteral("rich-insert-url"));
        auto *label = root->findChild<QQuickItem *>(QStringLiteral("rich-insert-label"));
        auto *browse = root->findChild<QObject *>(QStringLiteral("browse-image-button"));
        auto *picker = root->findChild<QObject *>(QStringLiteral("image-file-dialog"));
        if (!richDialog || !url || !label || !browse || !picker || !richDialog->property("visible").toBool())
            return 187;
        const int screenshotIndex = arguments.indexOf(QStringLiteral("--screenshot"));
        if (screenshotIndex >= 0 && screenshotIndex + 1 < arguments.size())
            window->grabWindow().save(arguments[screenshotIndex + 1]);
        const QPointF fieldRight = url->mapToScene(QPointF(url->width(), 0));
        const qreal dialogRight = richDialog->property("x").toReal() + richDialog->property("width").toReal();
        if (fieldRight.x() > dialogRight) return 188;
        if (!QMetaObject::invokeMethod(browse, "clicked")) return 189;
        settle();
        if (!picker->property("visible").toBool()) return 190;
        const QUrl image = QUrl::fromLocalFile(folder.filePath(QStringLiteral("image with spaces.png")));
        QImage sample(1, 1, QImage::Format_RGB32);
        sample.fill(Qt::white);
        if (!sample.save(image.toLocalFile())) return 204;
        picker->setProperty("selectedFile", image);
        QMetaObject::invokeMethod(picker, "close");
        if (!QMetaObject::invokeMethod(picker, "accepted")) return 191;
        settle();
        if (url->property("text").toString() != image.toString(QUrl::FullyEncoded)) { fprintf(stderr, "IMAGE URL %s expected %s selected %s\n", qPrintable(url->property("text").toString()), qPrintable(image.toString(QUrl::FullyEncoded)), qPrintable(picker->property("selectedFile").toUrl().toString())); return 192; }
        label->setProperty("text", QStringLiteral("Picture"));
        if (!QMetaObject::invokeMethod(richDialog, "accept")) return 193;
        settle();
        if (!QMetaObject::invokeMethod(root, "commitActive")
            || !controller.sourceText().contains(QStringLiteral("![Picture]("))) { fprintf(stderr, "INSERT SOURCE %s\n", qPrintable(controller.sourceText())); return 194; }
        settle();
        editor = findVisual(window->contentItem(), QStringLiteral("formatted-editor-0"));
        editor->forceActiveFocus();
        if (!QMetaObject::invokeMethod(root, "openInsertDialog", Q_ARG(QVariant, QVariant(QStringLiteral("link")))))
            return 195;
        url->setProperty("text", QStringLiteral("https://example.org"));
        label->setProperty("text", QStringLiteral("Link"));
        if (!QMetaObject::invokeMethod(richDialog, "accept")) return 196;
        settle();
        if (!QMetaObject::invokeMethod(root, "commitActive")
            || !controller.sourceText().contains(QStringLiteral("[Link](https://example.org)"))) return 197;
        for (bool source : {false, true}) {
            root->setProperty("sourceMode", source);
            if (!QMetaObject::invokeMethod(root, "newTable")) return 198;
            auto *table = root->findChild<QObject *>(QStringLiteral("new-table-dialog"));
            const QString before = controller.sourceText();
            if (!table || !QMetaObject::invokeMethod(table, "accept")) return 199;
            settle();
            if (controller.sourceText() == before || !controller.sourceText().contains(QStringLiteral("| --- | --- | --- |")))
                return 200;
        }
        if (!QMetaObject::invokeMethod(root, "newDocument")) return 201;
        settle();
        if (!window->activeFocusItem()
            || window->activeFocusItem()->objectName() != QStringLiteral("source-editor")) return 202;
        for (bool useButton : {false, true}) {
            if (!QMetaObject::invokeMethod(dialog, "open")) return 209;
            settle();
            const QString name = useButton ? QStringLiteral("button.md") : QStringLiteral("keypad.md");
            filename->setProperty("text", name);
            if (useButton) {
                if (!QMetaObject::invokeMethod(create, "clicked")) return 210;
                settle();
            } else pressEnter(Qt::Key_Enter);
            if (dialog->property("visible").toBool() || !QFileInfo::exists(folder.filePath(name))
                || !window->activeFocusItem()
                || window->activeFocusItem()->objectName() != QStringLiteral("source-editor")) return 211;
        }
        if (!qmlWarnings.isEmpty()) return 203;
        return 0;
    }
    if (arguments.contains(QStringLiteral("--folder-check"))) {
        QObject *root = engine.rootObjects().first();
        auto *window = qobject_cast<QQuickWindow *>(root);
        QObject *dialog = root->findChild<QObject *>(QStringLiteral("folder-dialog"));
        QObject *instruction = root->findChild<QObject *>(QStringLiteral("folder-instruction"));
        QObject *pathField = root->findChild<QObject *>(QStringLiteral("folder-path-field"));
        if (!window || !dialog || !instruction || !pathField || !QMetaObject::invokeMethod(dialog, "open"))
            return 140;
        QCoreApplication::processEvents();
        if (!instruction->property("visible").toBool()
            || instruction->property("text").toString() != QStringLiteral("Select a directory to open as a project.")
            || !dialog->property("title").toString().isEmpty())
            return 141;
        const int screenshotIndex = arguments.indexOf(QStringLiteral("--screenshot"));
        if (screenshotIndex >= 0 && screenshotIndex + 1 < arguments.size()) {
            const QString outputPath = arguments[screenshotIndex + 1];
            QTimer::singleShot(500, &app, [&app, window, outputPath] {
                window->grabWindow().save(outputPath);
                app.quit();
            });
            return app.exec();
        }
        QTemporaryDir selected;
        if (!selected.isValid()) return 142;
        if (!QDir(selected.path()).mkdir(QStringLiteral("child"))) return 147;
        pathField->setProperty("text", selected.path());
        if (!QMetaObject::invokeMethod(dialog, "navigateEnteredPath")) return 143;
        if (dialog->property("currentFolder").toUrl().toLocalFile() != selected.path()) return 144;
        QCoreApplication::processEvents();
        QEventLoop folderListingWait;
        QTimer::singleShot(250, &folderListingWait, &QEventLoop::quit);
        folderListingWait.exec();
        QQuickItem *entry = findVisual(window->contentItem(), QStringLiteral("folder-entry-child"));
        if (!entry || !QMetaObject::invokeMethod(entry, "clicked")) {
            fprintf(stderr, "FOLDER ENTRY: %p\n", static_cast<void *>(entry));
            return 148;
        }
        const QString childPath = QDir(selected.path()).filePath(QStringLiteral("child"));
        if (dialog->property("currentFolder").toUrl().toLocalFile() != childPath) return 149;
        if (!QMetaObject::invokeMethod(dialog, "accept")) return 145;
        return controller.projectFolder() == childPath ? 0 : 146;
    }
    if (arguments.contains(QStringLiteral("--formatted-check"))) {
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        QObject *editor = window ? findVisual(window->contentItem(), QStringLiteral("formatted-editor-0")) : nullptr;
        if (!editor) return 60;
        QString html = editor->property("text").toString();
        if (!html.contains(QStringLiteral("Results draft"))) return 61;
        html.replace(QStringLiteral("Results draft"), QStringLiteral("Results revised"));
        editor->setProperty("text", html);
        editor->setProperty("contentDirty", true);
        if (!QMetaObject::invokeMethod(editor, "commit")) return 62;
        if (!controller.sourceText().contains(QStringLiteral("# Results revised"))) return 63;
        if (!controller.sourceText().contains(QStringLiteral("| Alpha | 12 | *review* |"))) return 64;
        QCoreApplication::processEvents();
        editor = findVisual(window->contentItem(), QStringLiteral("formatted-editor-0"));
        auto *quickDocument = editor ? qobject_cast<QQuickTextDocument *>(
            editor->property("textDocument").value<QObject *>()) : nullptr;
        if (!quickDocument) return 66;
        const int start = quickDocument->textDocument()->toPlainText().indexOf(QStringLiteral("table below"));
        if (start < 0) return 67;
        controller.formatSelection(quickDocument, start, start + 5, QStringLiteral("bold"));
        editor->setProperty("contentDirty", true);
        if (!QMetaObject::invokeMethod(editor, "commit")) return 68;
        if (!controller.sourceText().contains(QStringLiteral("**table** below"))) return 69;
        controller.copyLink(QStringLiteral("https://example.org/source"));
        if (QGuiApplication::clipboard()->text() != QStringLiteral("https://example.org/source")) return 65;
        QObject *root = engine.rootObjects().first();
        if (!QMetaObject::invokeMethod(root, "offerLink", Q_ARG(QVariant, QVariant::fromValue(editor)),
                                       Q_ARG(QVariant, QVariant(QStringLiteral("https://example.org/source")))))
            return 70;
        QObject *actions = root->findChild<QObject *>(QStringLiteral("link-actions"));
        if (!actions || !actions->property("visible").toBool()) return 71;
        QObject *modeSwitch = root->findChild<QObject *>(QStringLiteral("mode-switch"));
        if (!modeSwitch) return 72;
        modeSwitch->setProperty("checked", true);
        if (!root->property("sourceMode").toBool()) return 73;
        modeSwitch->setProperty("checked", false);
        if (root->property("sourceMode").toBool()) return 74;
        return 0;
    }
    if (arguments.contains(QStringLiteral("--insert-check"))) {
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        QQuickItem *editor = window ? findVisual(window->contentItem(), QStringLiteral("formatted-editor-0")) : nullptr;
        if (!editor) return 80;
        auto *quickDocument = qobject_cast<QQuickTextDocument *>(
            editor->property("textDocument").value<QObject *>());
        if (!quickDocument) return 81;
        const int visualCursor = quickDocument->textDocument()->toPlainText().indexOf(QStringLiteral("table below"));
        if (visualCursor < 0) return 82;
        editor->setProperty("cursorPosition", visualCursor);
        editor->forceActiveFocus();
        QCoreApplication::processEvents();
        QObject *root = engine.rootObjects().first();
        if (!QMetaObject::invokeMethod(root, "newTable")) return 83;
        QObject *dialog = root->findChild<QObject *>(QStringLiteral("new-table-dialog"));
        if (!dialog || !QMetaObject::invokeMethod(dialog, "accept")) return 84;
        const QString source = controller.sourceText();
        const int newTable = source.indexOf(QStringLiteral("|  |  |  |\n| --- | --- | --- |"));
        const int trailingText = source.indexOf(QStringLiteral("table below"));
        const int oldTable = source.indexOf(QStringLiteral("| Sample | Count | Note |"));
        return newTable >= 0 && trailingText > newTable && oldTable > trailingText ? 0 : 85;
    }
    if (arguments.contains(QStringLiteral("--editing-check"))) {
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        QQuickItem *editor = window ? findVisual(window->contentItem(), QStringLiteral("formatted-editor-0")) : nullptr;
        if (!editor) return 90;
        auto *quickDocument = qobject_cast<QQuickTextDocument *>(
            editor->property("textDocument").value<QObject *>());
        if (!quickDocument) return 91;
        const int position = quickDocument->textDocument()->toPlainText().indexOf(QStringLiteral("An inline"));
        if (position < 0) return 92;
        editor->setProperty("cursorPosition", position);
        editor->forceActiveFocus();
        QCoreApplication::processEvents();
        QObject *root = engine.rootObjects().first();
        if (!QMetaObject::invokeMethod(root, "formatSelection", Q_ARG(QVariant, QVariant(QStringLiteral("bold")))))
            return 93;
        if (!editor->property("typingBold").toBool()) return 94;
        QKeyEvent boldLetter(QEvent::KeyPress, Qt::Key_X, Qt::NoModifier, QStringLiteral("X"));
        QCoreApplication::sendEvent(window, &boldLetter);
        if (!QMetaObject::invokeMethod(root, "formatSelection", Q_ARG(QVariant, QVariant(QStringLiteral("bold")))))
            return 95;
        if (editor->property("typingBold").toBool()) return 96;
        QKeyEvent plainLetter(QEvent::KeyPress, Qt::Key_Y, Qt::NoModifier, QStringLiteral("Y"));
        QCoreApplication::sendEvent(window, &plainLetter);
        if (!QMetaObject::invokeMethod(root, "formatSelection", Q_ARG(QVariant, QVariant(QStringLiteral("italic")))))
            return 99;
        QKeyEvent italicLetter(QEvent::KeyPress, Qt::Key_Z, Qt::NoModifier, QStringLiteral("Z"));
        QCoreApplication::sendEvent(window, &italicLetter);
        if (!QMetaObject::invokeMethod(root, "formatSelection", Q_ARG(QVariant, QVariant(QStringLiteral("italic")))))
            return 100;
        QKeyEvent finalPlainLetter(QEvent::KeyPress, Qt::Key_W, Qt::NoModifier, QStringLiteral("W"));
        QCoreApplication::sendEvent(window, &finalPlainLetter);
        if (!QMetaObject::invokeMethod(editor, "commit")) return 97;
        if (!controller.sourceText().contains(QStringLiteral("**X**Y*Z*WAn [inline"))) {
            fprintf(stderr, "STYLED SOURCE: %s\n", controller.sourceText().toUtf8().constData());
            return 98;
        }
        QCoreApplication::processEvents();
        editor = findVisual(window->contentItem(), QStringLiteral("formatted-editor-0"));
        quickDocument = editor ? qobject_cast<QQuickTextDocument *>(
            editor->property("textDocument").value<QObject *>()) : nullptr;
        if (!quickDocument) return 101;
        const QString beforeBackspace = quickDocument->textDocument()->toPlainText();
        const int lineEnd = beforeBackspace.indexOf(QStringLiteral(" text."));
        if (lineEnd < 0) return 102;
        editor->setProperty("cursorPosition", lineEnd + 6);
        editor->forceActiveFocus();
        QCoreApplication::processEvents();
        if (!QMetaObject::invokeMethod(root, "formatSelection", Q_ARG(QVariant, QVariant(QStringLiteral("bold")))))
            return 103;
        QKeyEvent backspace(QEvent::KeyPress, Qt::Key_Backspace, Qt::NoModifier, QStringLiteral("\b"));
        QCoreApplication::sendEvent(window, &backspace);
        const QString afterBackspace = quickDocument->textDocument()->toPlainText();
        if (afterBackspace != QString(beforeBackspace).remove(lineEnd + 5, 1)) return 104;
        if (afterBackspace.contains(QLatin1Char('\b'))) return 105;
        if (!QMetaObject::invokeMethod(editor, "commit")) return 106;
        if (controller.sourceText().contains(QLatin1Char('\b'))) return 107;
        return 0;
    }
    if (arguments.contains(QStringLiteral("--heading-check"))) {
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        controller.newDocument();
        QCoreApplication::processEvents();
        QQuickItem *editor = window ? findVisual(window->contentItem(), QStringLiteral("formatted-editor-0")) : nullptr;
        if (!editor) return 270;
        editor->forceActiveFocus();
        QCoreApplication::processEvents();
        auto type = [&](const QString &text) {
            for (const QChar ch : text) {
                const int key = ch == QLatin1Char(' ') ? Qt::Key_Space : ch == QLatin1Char('\n') ? Qt::Key_Return
                    : ch == QLatin1Char('#') ? Qt::Key_NumberSign : Qt::Key_A;
                QKeyEvent press(QEvent::KeyPress, key, Qt::NoModifier, ch == QLatin1Char('\n') ? QStringLiteral("\r") : QString(ch));
                QCoreApplication::sendEvent(window, &press);
                QKeyEvent release(QEvent::KeyRelease, key, Qt::NoModifier, ch == QLatin1Char('\n') ? QStringLiteral("\r") : QString(ch));
                QCoreApplication::sendEvent(window, &release);
            }
        };
        type(QStringLiteral("## Title\nbody"));
        if (!QMetaObject::invokeMethod(editor, "commit")) return 271;
        const QString source = controller.sourceText();
        if (!source.startsWith(QStringLiteral("## Title\n\nbody"))) {
            fprintf(stderr, "HEADING SOURCE: %s\n", source.toUtf8().constData());
            return 272;
        }
        // Hashes that are not directly before the caret, and seven hashes, stay literal text.
        controller.newDocument();
        QCoreApplication::processEvents();
        editor = findVisual(window->contentItem(), QStringLiteral("formatted-editor-0"));
        if (!editor) return 273;
        editor->forceActiveFocus();
        type(QStringLiteral("####### x"));
        if (!QMetaObject::invokeMethod(editor, "commit")) return 274;
        if (!controller.sourceText().contains(QStringLiteral("####### x"))
            && !controller.sourceText().contains(QStringLiteral("\\#\\#\\#\\#\\#\\#\\# x"))) {
            fprintf(stderr, "SEVEN HASHES SOURCE: %s\n", controller.sourceText().toUtf8().constData());
            return 275;
        }
        return 0;
    }
    if (arguments.contains(QStringLiteral("--new-table-keys-check"))) {
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        QObject *root = engine.rootObjects().first();
        controller.newDocument();
        auto settle = [] {
            QEventLoop wait;
            QTimer::singleShot(150, &wait, &QEventLoop::quit);
            wait.exec();
        };
        settle();
        QQuickItem *editor = window ? findVisual(window->contentItem(), QStringLiteral("formatted-editor-0")) : nullptr;
        if (!editor) return 280;
        editor->forceActiveFocus();
        if (!QMetaObject::invokeMethod(root, "newTable")) return 281;
        settle();
        auto press = [&](Qt::Key key, const QString &text = QString()) {
            QKeyEvent down(QEvent::KeyPress, key, Qt::NoModifier, text);
            QCoreApplication::sendEvent(window, &down);
            QKeyEvent up(QEvent::KeyRelease, key, Qt::NoModifier, text);
            QCoreApplication::sendEvent(window, &up);
            settle();
        };
        // Rows is focused with its value selected: type, Tab, type, Tab to Insert, Enter.
        press(Qt::Key_5, QStringLiteral("5"));
        press(Qt::Key_Tab);
        press(Qt::Key_2, QStringLiteral("2"));
        press(Qt::Key_Tab);
        press(Qt::Key_Return);
        settle();
        const QString source = controller.sourceText();
        if (source.count(QStringLiteral("| --- | --- |")) != 1 || source.count(QLatin1Char('\n')) < 6) {
            fprintf(stderr, "TABLE SOURCE: %s\n", source.toUtf8().constData());
            return 282;
        }
        // 5 rows (header + 4 body) and 2 columns; delimiter row included.
        if (source.split(QLatin1Char('\n'), Qt::SkipEmptyParts).size() != 6) return 283;
        QQuickItem *cell = findVisual(window->contentItem(), QStringLiteral("table-cell-0-0-0"));
        if (!cell || !cell->hasActiveFocus()) return 284;
        // Enter inside a number field confirms too, and inserts exactly one table.
        if (!QMetaObject::invokeMethod(root, "newTable")) return 285;
        settle();
        press(Qt::Key_4, QStringLiteral("4"));
        press(Qt::Key_Return);
        settle();
        const QString second = controller.sourceText();
        // The dialog keeps the last column count (2); only the typed row count (4) changes.
        if (second.count(QStringLiteral("| --- | --- |")) != 2) return 286;
        if (second.split(QLatin1Char('\n'), Qt::SkipEmptyParts).size() != 6 + 5) return 288;
        QQuickItem *secondCell = findVisual(window->contentItem(), QStringLiteral("table-cell-1-0-0"));
        if (!secondCell || !secondCell->hasActiveFocus()) return 287;
        return 0;
    }
    if (arguments.contains(QStringLiteral("--picker-keys-check"))) {
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        QObject *root = engine.rootObjects().first();
        QTemporaryDir tree;
        if (!window || !tree.isValid()) return 290;
        QDir(tree.path()).mkpath(QStringLiteral("alpha"));
        QDir(tree.path()).mkpath(QStringLiteral("beta/inner"));
        controller.setProjectFolder(tree.path());
        window->requestActivate();
        auto settle = [] {
            QEventLoop wait;
            QTimer::singleShot(200, &wait, &QEventLoop::quit);
            wait.exec();
        };
        auto press = [&](Qt::Key key, const QString &text = QString()) {
            QKeyEvent down(QEvent::KeyPress, key, Qt::NoModifier, text);
            QCoreApplication::sendEvent(window, &down);
            QKeyEvent up(QEvent::KeyRelease, key, Qt::NoModifier, text);
            QCoreApplication::sendEvent(window, &up);
            settle();
        };
        auto list = [&](const char *name) { return findVisual(window->contentItem(), QString::fromLatin1(name)); };
        // Folder dialog: Down selects, Enter goes in, Left goes up, Tab reaches the button, Enter opens.
        QObject *folder = root->findChild<QObject *>(QStringLiteral("folder-dialog"));
        if (!folder || !QMetaObject::invokeMethod(folder, "open")) return 291;
        settle();
        QQuickItem *folderList = list("folder-list");
        if (!folderList || !folderList->hasActiveFocus() || folderList->property("currentIndex").toInt() != 0) return 292;
        press(Qt::Key_Down);
        if (folderList->property("currentIndex").toInt() != 1) return 293;
        press(Qt::Key_Return);
        if (folder->property("currentFolder").toUrl().toLocalFile() != tree.filePath(QStringLiteral("beta"))) return 294;
        if (folderList->property("currentIndex").toInt() != 0) return 295;
        press(Qt::Key_Left);
        if (folder->property("currentFolder").toUrl().toLocalFile() != tree.path()
            || folderList->property("currentIndex").toInt() != 1) return 296;
        press(Qt::Key_A, QStringLiteral("a"));
        if (folderList->property("currentIndex").toInt() != 0) return 297;
        press(Qt::Key_Return);
        if (folder->property("currentFolder").toUrl().toLocalFile() != tree.filePath(QStringLiteral("alpha"))) return 298;
        press(Qt::Key_Tab);
        QObject *accept = nullptr;
        for (auto *button : root->findChildren<QObject *>(QStringLiteral("dialog-accept")))
            if (button->property("text").toString() == QStringLiteral("Open folder")) accept = button;
        if (!accept || !accept->property("activeFocus").toBool()) return 299;
        press(Qt::Key_Return);
        if (folder->property("visible").toBool() || controller.projectFolder() != tree.filePath(QStringLiteral("alpha"))) return 300;

        // Save as: Enter saves a new name; saving over a file asks first, with Replace focused.
        controller.setProjectFolder(tree.path());
        controller.newDocument();
        controller.setSourceText(QStringLiteral("first"));
        QObject *save = root->findChild<QObject *>(QStringLiteral("save-file-dialog"));
        QObject *overwrite = root->findChild<QObject *>(QStringLiteral("overwrite-dialog"));
        if (!save || !overwrite || !QMetaObject::invokeMethod(save, "open")) return 301;
        settle();
        QQuickItem *name = list("save-file-name");
        if (!name || !name->hasActiveFocus()) return 302;
        name->setProperty("text", QStringLiteral("out.md"));
        press(Qt::Key_Return);
        if (save->property("visible").toBool()) return 303;
        QFile saved(tree.filePath(QStringLiteral("out.md")));
        if (!saved.open(QIODevice::ReadOnly) || saved.readAll() != "first") return 304;
        saved.close();
        controller.setSourceText(QStringLiteral("second"));
        if (!QMetaObject::invokeMethod(save, "open")) return 305;
        settle();
        name = list("save-file-name");
        name->setProperty("text", QStringLiteral("out.md"));
        press(Qt::Key_Return);
        if (!overwrite->property("visible").toBool()) return 306;
        press(Qt::Key_Return);
        if (overwrite->property("visible").toBool() || save->property("visible").toBool()) return 307;
        if (!saved.open(QIODevice::ReadOnly) || saved.readAll() != "second") return 308;
        return 0;
    }
    if (arguments.contains(QStringLiteral("--navigation-check"))) {
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        QObject *root = engine.rootObjects().first();
        QTemporaryDir tree;
        if (!window || !tree.isValid()) return 310;
        for (const QString &name : {QStringLiteral("a.md"), QStringLiteral("b.md"), QStringLiteral("c.md")}) {
            QFile file(tree.filePath(name));
            if (!file.open(QIODevice::WriteOnly) || file.write("Above\n\n| A | B |\n| --- | --- |\n| 1 | 2 |\n\nBelow\n") < 0)
                return 311;
        }
        window->requestActivate();
        auto settle = [] {
            QEventLoop wait;
            QTimer::singleShot(200, &wait, &QEventLoop::quit);
            wait.exec();
        };
        auto press = [&](Qt::Key key, Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
            QKeyEvent down(QEvent::KeyPress, key, modifiers);
            QCoreApplication::sendEvent(window, &down);
            QKeyEvent up(QEvent::KeyRelease, key, modifiers);
            QCoreApplication::sendEvent(window, &up);
            settle();
        };
        // Project list: focused after opening a folder; arrows move, Shift+Down extends, Enter opens.
        controller.setProjectFolder(tree.path());
        if (!QMetaObject::invokeMethod(root, "focusFileList")) return 312;
        settle();
        QQuickItem *files = findVisual(window->contentItem(), QStringLiteral("project-list"));
        if (!files || !files->hasActiveFocus() || files->property("currentIndex").toInt() != 0) return 313;
        {
            // A file created on disk appears in the visible list within the settle time.
            const int before = files->property("count").toInt();
            QFile added(tree.filePath(QStringLiteral("zz-added.md")));
            if (!added.open(QIODevice::WriteOnly)) return 330;
            added.close();
            settle();
            if (files->property("count").toInt() != before + 1) return 331;
        }
        press(Qt::Key_Down);
        if (files->property("currentIndex").toInt() != 1) return 314;
        press(Qt::Key_Down, Qt::ShiftModifier);
        press(Qt::Key_Return);
        int opened = 0;
        for (const QVariant &tab : controller.tabs())
            ++opened;
        if (opened != 2) return 315;
        {
            QQuickItem *openedEditor = findVisual(window->contentItem(), QStringLiteral("formatted-editor-0"));
            if (!openedEditor || !openedEditor->hasActiveFocus() || openedEditor->property("cursorPosition").toInt() != 0) {
                fprintf(stderr, "FOCUS: editor=%p focus=%d cursor=%d active=%s\n", static_cast<void *>(openedEditor),
                        openedEditor ? openedEditor->hasActiveFocus() : -1,
                        openedEditor ? openedEditor->property("cursorPosition").toInt() : -1,
                        window->activeFocusItem() ? qPrintable(window->activeFocusItem()->objectName()) : "none");
                return 326;
            }
        }
        // Tables: Down at the end of the text above enters the first cell, Up from below the last row.
        auto editorAt = [&](int block) {
            return findVisual(window->contentItem(), QStringLiteral("formatted-editor-%1").arg(block));
        };
        QQuickItem *above = editorAt(0);
        if (!above) return 316;
        above->setProperty("cursorPosition", above->property("length").toInt());
        above->forceActiveFocus();
        settle();
        press(Qt::Key_Down);
        QQuickItem *firstCell = findVisual(window->contentItem(), QStringLiteral("table-cell-0-0-0"));
        if (!firstCell || !firstCell->hasActiveFocus() || firstCell->property("cursorPosition").toInt() != 0) return 317;
        QQuickItem *below = editorAt(2);
        if (!below) return 318;
        below->setProperty("cursorPosition", 0);
        below->forceActiveFocus();
        settle();
        press(Qt::Key_Up);
        QQuickItem *lastRowCell = findVisual(window->contentItem(), QStringLiteral("table-cell-0-1-0"));
        if (!lastRowCell || !lastRowCell->hasActiveFocus() || lastRowCell->property("cursorPosition").toInt() != 0) return 319;
        // Shift+Down at the end of the text above selects the table; Shift+Up takes it back.
        above = editorAt(0);
        above->setProperty("cursorPosition", above->property("length").toInt());
        above->forceActiveFocus();
        settle();
        press(Qt::Key_Down, Qt::ShiftModifier);
        if (root->property("selectedTable").toInt() != 0) return 320;
        press(Qt::Key_Up, Qt::ShiftModifier);
        if (root->property("selectedTable").toInt() != -1) return 321;
        press(Qt::Key_Down, Qt::ShiftModifier);
        press(Qt::Key_Delete);
        if (controller.sourceText().contains(QStringLiteral("| A |"))
            || !controller.sourceText().contains(QStringLiteral("Above")) || !controller.sourceText().contains(QStringLiteral("Below"))
            || controller.sourceText().contains(QStringLiteral("\n\n\n"))) {
            fprintf(stderr, "AFTER DELETE: [%s]\n", controller.sourceText().toUtf8().constData());
            return 322;
        }
        controller.undo();
        settle();
        if (!controller.sourceText().contains(QStringLiteral("| A | B |"))) return 323;
        // Dragging the mouse from the text above to the text below selects the table.
        above = editorAt(0);
        below = editorAt(2);
        QQuickItem *tableCell = findVisual(window->contentItem(), QStringLiteral("table-cell-0-1-0"));
        if (!above || !below || !tableCell) return 324;
        const QPointF from = above->mapToScene(QPointF(10, above->height() / 2));
        const QPointF to = below->mapToScene(QPointF(10, below->height() / 2));
        QMouseEvent pressEvent(QEvent::MouseButtonPress, from, from, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(window, &pressEvent);
        for (int step = 1; step <= 10; ++step) {
            const QPointF at = from + (to - from) * (step / 10.0);
            QMouseEvent move(QEvent::MouseMove, at, at, Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
            QCoreApplication::sendEvent(window, &move);
        }
        settle();
        const int dragged = root->property("selectedTable").toInt();
        QMouseEvent releaseEvent(QEvent::MouseButtonRelease, to, to, Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        QCoreApplication::sendEvent(window, &releaseEvent);
        if (dragged != 0) return 325;
        // A single file opened with Enter gets the caret at its start, even when it begins with a table.
        {
            QTemporaryDir second;
            if (!second.isValid()) return 327;
            QFile tableFirst(second.filePath(QStringLiteral("a-table.md")));
            QFile textFirst(second.filePath(QStringLiteral("b-text.md")));
            if (!tableFirst.open(QIODevice::WriteOnly) || tableFirst.write("| A | B |\n| --- | --- |\n| 1 | 2 |\n\nAfter\n") < 0
                || !textFirst.open(QIODevice::WriteOnly) || textFirst.write("Start of text\n\nMore\n") < 0)
                return 328;
            tableFirst.close();
            textFirst.close();
            controller.setProjectFolder(second.path());
            QMetaObject::invokeMethod(root, "focusFileList");
            settle();
            QQuickItem *list = findVisual(window->contentItem(), QStringLiteral("project-list"));
            if (!list || !list->hasActiveFocus()) return 329;
            press(Qt::Key_Return);
            QQuickItem *cell = findVisual(window->contentItem(), QStringLiteral("table-cell-0-0-0"));
            if (!cell || !cell->hasActiveFocus()) return 330;
            QMetaObject::invokeMethod(root, "focusFileList");
            settle();
            press(Qt::Key_Down);
            press(Qt::Key_Return);
            QQuickItem *text = findVisual(window->contentItem(), QStringLiteral("formatted-editor-0"));
            if (!text || !text->hasActiveFocus() || text->property("cursorPosition").toInt() != 0) return 331;
        }
        return 0;
    }
    if (arguments.contains(QStringLiteral("--insert-actions-check"))) {
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        QObject *root = engine.rootObjects().first();
        auto placeCaret = [&]() -> bool {
            QCoreApplication::processEvents();
            QQuickItem *editor = window ? findVisual(window->contentItem(), QStringLiteral("formatted-editor-0")) : nullptr;
            if (!editor) return false;
            auto *document = qobject_cast<QQuickTextDocument *>(editor->property("textDocument").value<QObject *>());
            if (!document) return false;
            const int position = document->textDocument()->toPlainText().indexOf(QStringLiteral("An inline"));
            if (position < 0) return false;
            editor->setProperty("cursorPosition", position);
            editor->forceActiveFocus();
            QCoreApplication::processEvents();
            return true;
        };
        auto insert = [&](const QString &kind, const QString &url, const QString &label) -> bool {
            if (!placeCaret() || !QMetaObject::invokeMethod(root, "openInsertDialog", Q_ARG(QVariant, QVariant(kind))))
                return false;
            QObject *dialog = root->findChild<QObject *>(QStringLiteral("rich-insert-dialog"));
            QObject *urlField = root->findChild<QObject *>(QStringLiteral("rich-insert-url"));
            QObject *labelField = root->findChild<QObject *>(QStringLiteral("rich-insert-label"));
            if (!dialog || !urlField || !labelField) return false;
            urlField->setProperty("text", url);
            labelField->setProperty("text", label);
            if (!QMetaObject::invokeMethod(dialog, "accept")) return false;
            QCoreApplication::processEvents();
            return QMetaObject::invokeMethod(root, "commitActive");
        };
        if (!insert(QStringLiteral("link"), QStringLiteral("https://example.org/new"), QStringLiteral("New"))) return 110;
        if (!controller.sourceText().contains(QStringLiteral("[New](https://example.org/new)"))) return 111;
        if (!controller.reloadCurrent(true)) return 112;
        if (!insert(QStringLiteral("image"), QStringLiteral("figures/new.png"), QStringLiteral("Alt"))) return 113;
        if (!controller.sourceText().contains(QStringLiteral("![Alt](figures/new.png)"))) return 114;
        if (!controller.reloadCurrent(true) || !placeCaret()) return 115;
        if (!QMetaObject::invokeMethod(root, "toggleList", Q_ARG(QVariant, QVariant(false)))) return 116;
        if (!QMetaObject::invokeMethod(root, "commitActive")) return 117;
        if (!controller.sourceText().contains(QStringLiteral("- An [inline"))) return 118;
        if (!controller.reloadCurrent(true) || !placeCaret()) return 119;
        if (!QMetaObject::invokeMethod(root, "toggleList", Q_ARG(QVariant, QVariant(true)))) return 120;
        if (!QMetaObject::invokeMethod(root, "commitActive")) return 121;
        if (!controller.sourceText().contains(QRegularExpression(QStringLiteral("(?m)^1\\. +An \\[inline")))) {
            fprintf(stderr, "NUMBERED SOURCE: %s\n", controller.sourceText().toUtf8().constData());
            return 122;
        }
        return 0;
    }
    if (arguments.contains(QStringLiteral("--shortcuts-check"))) {
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        QObject *root = engine.rootObjects().first();
        QObject *folder = root->findChild<QObject *>(QStringLiteral("folder-dialog"));
        QObject *link = root->findChild<QObject *>(QStringLiteral("rich-insert-dialog"));
        if (!window || !folder || !link) return 130;
        window->requestActivate();
        QCoreApplication::processEvents();
        auto press = [&](Qt::Key key, Qt::KeyboardModifiers modifiers) {
            QKeyEvent down(QEvent::KeyPress, key, modifiers);
            QCoreApplication::sendEvent(window, &down);
            QKeyEvent up(QEvent::KeyRelease, key, modifiers);
            QCoreApplication::sendEvent(window, &up);
            QCoreApplication::processEvents();
        };
        press(Qt::Key_O, Qt::ControlModifier);
        if (!folder->property("visible").toBool()) return 131;
        QMetaObject::invokeMethod(folder, "close");
        QCoreApplication::processEvents();
        window->requestActivate();
        QCoreApplication::processEvents();
        press(Qt::Key_O, Qt::ControlModifier | Qt::ShiftModifier);
        if (!folder->property("visible").toBool()) return 132;
        QMetaObject::invokeMethod(folder, "close");
        QCoreApplication::processEvents();
        window->requestActivate();
        QCoreApplication::processEvents();
        press(Qt::Key_K, Qt::ControlModifier);
        press(Qt::Key_O, Qt::ControlModifier);
        if (!folder->property("visible").toBool() || link->property("visible").toBool()) return 133;
        QMetaObject::invokeMethod(folder, "close");
        QCoreApplication::processEvents();
        window->requestActivate();
        QCoreApplication::processEvents();
        QQuickItem *editor = findVisual(window->contentItem(), QStringLiteral("formatted-editor-0"));
        if (!editor) return 135;
        editor->forceActiveFocus();
        QCoreApplication::processEvents();
        press(Qt::Key_K, Qt::ControlModifier | Qt::AltModifier);
        if (!link->property("visible").toBool()) return 134;
        return 0;
    }
    if (arguments.contains(QStringLiteral("--table-typing-check"))) {
        QTimer::singleShot(500, &app, [&app, &engine, &controller] {
            auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
            if (!window) { app.exit(150); return; }
            auto cell = [&](int row, int col) -> QQuickItem * {
                return findVisual(window->contentItem(), QStringLiteral("table-cell-0-%1-%2").arg(row).arg(col));
            };
            auto key = [&](Qt::Key code, const QString &typed = QString()) {
                QKeyEvent press(QEvent::KeyPress, code, Qt::NoModifier, typed);
                QCoreApplication::sendEvent(window, &press);
                QCoreApplication::processEvents();
            };
            QQuickItem *first = cell(1, 0);
            if (!first) { app.exit(151); return; }
            first->forceActiveFocus();
            QCoreApplication::processEvents();
            key(Qt::Key_X, QStringLiteral("x"));
            if (!first->property("text").toString().contains(QLatin1Char('x'))) { app.exit(152); return; }
            key(Qt::Key_Tab);
            QQuickItem *right = cell(1, 1);
            if (!right || !right->hasActiveFocus()) {
                fprintf(stderr, "TAB AFTER TYPING: right=%p focus=%d active=%s source=%s\n", static_cast<void *>(right),
                    right ? right->hasActiveFocus() : 0,
                    window->activeFocusItem() ? qPrintable(window->activeFocusItem()->objectName()) : "none",
                    controller.sourceText().toUtf8().constData());
                app.exit(153); return;
            }
            key(Qt::Key_Y, QStringLiteral("y"));
            key(Qt::Key_Down);
            QQuickItem *below = cell(2, 1);
            if (!below || !below->hasActiveFocus()) { app.exit(154); return; }
            key(Qt::Key_Z, QStringLiteral("z"));
            key(Qt::Key_Up);
            right = cell(1, 1);
            if (!right || !right->hasActiveFocus()) { app.exit(155); return; }
            QQuickItem *last = cell(2, 2);
            if (!last) { app.exit(157); return; }
            last->forceActiveFocus();
            QCoreApplication::processEvents();
            key(Qt::Key_V, QStringLiteral("v"));
            key(Qt::Key_Tab);
            QQuickItem *newRow = cell(3, 0);
            if (!newRow || !newRow->hasActiveFocus()) { app.exit(158); return; }
            key(Qt::Key_W, QStringLiteral("w"));
            key(Qt::Key_Backtab);
            last = cell(2, 2);
            if (!last || !last->hasActiveFocus()) { app.exit(159); return; }
            QQuickItem *bottom = cell(3, 1);
            if (!bottom) { app.exit(160); return; }
            bottom->forceActiveFocus();
            QCoreApplication::processEvents();
            key(Qt::Key_B, QStringLiteral("b"));
            key(Qt::Key_Down);
            QQuickItem *textBelow = findVisual(window->contentItem(), QStringLiteral("formatted-editor-2"));
            if (!textBelow || !textBelow->hasActiveFocus() || textBelow->property("cursorPosition").toInt() != 0) {
                app.exit(161); return;
            }
            QQuickItem *top = cell(0, 1);
            if (!top) { app.exit(162); return; }
            top->forceActiveFocus();
            QCoreApplication::processEvents();
            key(Qt::Key_H, QStringLiteral("h"));
            key(Qt::Key_Up);
            QQuickItem *textAbove = findVisual(window->contentItem(), QStringLiteral("formatted-editor-0"));
            if (!textAbove || !textAbove->hasActiveFocus()
                || textAbove->property("cursorPosition").toInt() != textAbove->property("length").toInt()) {
                fprintf(stderr, "UP OUT: item=%p focused=%d pos=%d length=%d active=%s source=%s\n",
                    static_cast<void *>(textAbove), textAbove ? textAbove->hasActiveFocus() : 0,
                    textAbove ? textAbove->property("cursorPosition").toInt() : -1,
                    textAbove ? textAbove->property("length").toInt() : -1,
                    window->activeFocusItem() ? qPrintable(window->activeFocusItem()->objectName()) : "none",
                    controller.sourceText().toUtf8().constData());
                app.exit(163); return;
            }
            app.exit(controller.sourceText().contains(QLatin1Char('x'))
                && controller.sourceText().contains(QLatin1Char('y'))
                && controller.sourceText().contains(QLatin1Char('z'))
                && controller.sourceText().contains(QLatin1Char('v'))
                && controller.sourceText().contains(QLatin1Char('w'))
                && controller.sourceText().contains(QLatin1Char('b'))
                && controller.sourceText().contains(QLatin1Char('h')) ? 0 : 156);
        });
        return app.exec();
    }
    if (arguments.contains(QStringLiteral("--table-actions-check"))) {
        QTimer::singleShot(500, &app, [&app, &engine, &controller] {
            auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
            if (!window) { app.exit(170); return; }
            for (const QString &name : {QStringLiteral("table-insert-row-above"),
                                        QStringLiteral("table-insert-row-below"),
                                        QStringLiteral("table-insert-col-left"),
                                        QStringLiteral("table-insert-col-right"),
                                        QStringLiteral("table-delete-row"),
                                        QStringLiteral("table-delete-col")}) {
                QQuickItem *button = findVisual(window->contentItem(), name);
                if (!button || !button->isVisible() || button->property("tip").toString().isEmpty()) {
                    app.exit(171); return;
                }
            }
            auto dimensions = [&]() -> QPair<int, int> {
                for (const QVariant &block : controller.blocks()) {
                    const QVariantMap data = block.toMap();
                    if (data.value(QStringLiteral("kind")).toString() == QStringLiteral("table"))
                        return {data.value(QStringLiteral("rows")).toList().size(),
                                data.value(QStringLiteral("columns")).toInt()};
                }
                return {-1, -1};
            };
            auto focused = [&](int row, int col) -> bool {
                QQuickItem *cell = findVisual(window->contentItem(),
                    QStringLiteral("table-cell-0-%1-%2").arg(row).arg(col));
                return cell && cell->hasActiveFocus();
            };
            QQuickItem *cell = findVisual(window->contentItem(), QStringLiteral("table-cell-0-1-1"));
            if (!cell) { app.exit(172); return; }
            cell->forceActiveFocus();
            QCoreApplication::processEvents();
            auto key = [&](Qt::Key code, Qt::KeyboardModifiers modifiers) {
                QKeyEvent press(QEvent::KeyPress, code, modifiers);
                QCoreApplication::sendEvent(window, &press);
                QCoreApplication::processEvents();
            };
            key(Qt::Key_Down, Qt::ControlModifier | Qt::AltModifier);
            if (dimensions() != QPair<int, int>(4, 3) || !focused(2, 1)) { app.exit(173); return; }
            key(Qt::Key_Right, Qt::ControlModifier | Qt::AltModifier);
            if (dimensions() != QPair<int, int>(4, 4) || !focused(2, 2)) { app.exit(174); return; }
            key(Qt::Key_Up, Qt::ControlModifier | Qt::AltModifier);
            if (dimensions() != QPair<int, int>(5, 4) || !focused(2, 2)) {
                fprintf(stderr, "ROW ABOVE: rows=%d cols=%d focus=%s\n", dimensions().first,
                    dimensions().second,
                    window->activeFocusItem() ? qPrintable(window->activeFocusItem()->objectName()) : "none");
                app.exit(175); return;
            }
            key(Qt::Key_Left, Qt::ControlModifier | Qt::AltModifier);
            if (dimensions() != QPair<int, int>(5, 5) || !focused(2, 2)) { app.exit(176); return; }
            key(Qt::Key_Up, Qt::ControlModifier | Qt::AltModifier | Qt::ShiftModifier);
            if (dimensions() != QPair<int, int>(4, 5) || !focused(1, 2)) { app.exit(177); return; }
            key(Qt::Key_Left, Qt::ControlModifier | Qt::AltModifier | Qt::ShiftModifier);
            app.exit(dimensions() == QPair<int, int>(4, 4) && focused(1, 1) ? 0 : 178);
        });
        return app.exec();
    }
    if (arguments.contains(QStringLiteral("--pending-edit-check"))) {
        QTimer::singleShot(500, &app, [&app, &engine, &controller] {
            auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
            QObject *root = engine.rootObjects().first();
            if (!window) { app.exit(340); return; }
            auto settle = [] { QEventLoop wait; QTimer::singleShot(150, &wait, &QEventLoop::quit); wait.exec(); };
            auto type = [&](const QString &text) {
                for (const QChar &c : text) {
                    QKeyEvent down(QEvent::KeyPress, 0, Qt::NoModifier, QString(c));
                    QCoreApplication::sendEvent(window, &down);
                    QKeyEvent up(QEvent::KeyRelease, 0, Qt::NoModifier, QString(c));
                    QCoreApplication::sendEvent(window, &up);
                }
                settle();
            };
            auto tabDirty = [&controller](int index) {
                const QVariantList tabs = controller.tabs();
                return index < tabs.size() && tabs[index].toMap().value(QStringLiteral("dirty")).toBool();
            };
            window->requestActivate();
            QQuickItem *editor = findVisual(window->contentItem(), QStringLiteral("formatted-editor-0"));
            if (!editor) { app.exit(341); return; }
            editor->forceActiveFocus();
            editor->setProperty("cursorPosition", editor->property("length").toInt());
            type(QStringLiteral(" first-words"));
            // Saving or undoing commits while the editor keeps focus; later typing must still be kept.
            QMetaObject::invokeMethod(root, "commitEdits", Q_ARG(QVariant, true));
            settle();
            if (!controller.sourceText().contains(QStringLiteral("first-words"))) { app.exit(342); return; }
            {
                QQuickItem *again = findVisual(window->contentItem(), QStringLiteral("formatted-editor-0"));
                if (!again || !again->hasActiveFocus()) { app.exit(347); return; }
            }
            type(QStringLiteral(" unfinished-sentence"));
            if (!root->property("pendingEdit").toBool()) { app.exit(343); return; }
            // Switching tabs the way a tab click does must keep the half-written text.
            QMetaObject::invokeMethod(root, "commitActive");
            if (!controller.sourceText().contains(QStringLiteral("unfinished-sentence"))) { app.exit(344); return; }
            if (!tabDirty(0)) { app.exit(345); return; }
            controller.newDocument();
            settle();
            controller.selectTab(0);
            settle();
            if (!controller.sourceText().simplified().contains(QStringLiteral("first-words unfinished-sentence"))) { app.exit(346); return; }
            // A new blank document: a sentence typed without pressing Enter survives a tab switch.
            controller.newDocument();
            settle();
            QMetaObject::invokeMethod(root, "focusDocument");
            settle();
            type(QStringLiteral("half a sentence"));
            if (!root->property("pendingEdit").toBool()) { app.exit(348); return; }
            QMetaObject::invokeMethod(root, "commitActive");
            controller.selectTab(0);
            settle();
            controller.selectTab(controller.tabs().size() - 1);
            settle();
            if (!controller.sourceText().contains(QStringLiteral("half a sentence"))) { app.exit(349); return; }
            // Enter at the end of the text, then Save: the new empty line stays and typing continues on it.
            controller.newDocument();
            settle();
            QMetaObject::invokeMethod(root, "focusDocument");
            settle();
            type(QStringLiteral("Hello world"));
            {
                QKeyEvent down(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier, QStringLiteral("\r"));
                QCoreApplication::sendEvent(window, &down);
                QKeyEvent up(QEvent::KeyRelease, Qt::Key_Return, Qt::NoModifier, QStringLiteral("\r"));
                QCoreApplication::sendEvent(window, &up);
            }
            settle();
            QMetaObject::invokeMethod(root, "commitEdits", Q_ARG(QVariant, true));
            settle();
            if (controller.sourceText() != QStringLiteral("Hello world\n\n")) { app.exit(350); return; }
            QQuickItem *blank = findVisual(window->contentItem(), QStringLiteral("formatted-editor-0"));
            if (!blank || !blank->hasActiveFocus() || blank->property("cursorPosition").toInt() != blank->property("length").toInt()
                || blank->property("length").toInt() != 12) { app.exit(351); return; }
            type(QStringLiteral("next"));
            QMetaObject::invokeMethod(root, "commitActive");
            if (!controller.sourceText().simplified().contains(QStringLiteral("Hello world next"))) { app.exit(352); return; }
            app.exit(0);
        });
        return app.exec();
    }
    if (arguments.contains(QStringLiteral("--recent-check"))) {
        QTimer::singleShot(500, &app, [&app, &engine, &controller] {
            auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
            QObject *root = engine.rootObjects().first();
            QTemporaryDir settings, tree;
            if (!window || !settings.isValid() || !tree.isValid()) { app.exit(360); return; }
            QSettings::setPath(QSettings::NativeFormat, QSettings::UserScope, settings.path());
            controller.setRememberFolders(true);
            auto settle = [] { QEventLoop wait; QTimer::singleShot(100, &wait, &QEventLoop::quit); wait.exec(); };
            QStringList made;
            for (int i = 0; i < 12; ++i) {
                const QString dir = tree.filePath(QStringLiteral("project-%1").arg(i));
                QDir().mkpath(dir);
                made.push_back(QFileInfo(dir).canonicalFilePath());
                controller.setProjectFolder(dir);
            }
            QStringList recent = controller.recentFolders();
            if (recent.size() != 10 || recent.first() != made.last() || recent.last() != made[2]) { app.exit(361); return; }
            controller.setProjectFolder(made[5]);
            if (controller.recentFolders().first() != made[5] || controller.recentFolders().size() != 10) { app.exit(362); return; }
            // A fresh controller reads the saved list back.
            {
                Controller later;
                later.setRememberFolders(true);
                if (later.recentFolders() != controller.recentFolders()) { app.exit(363); return; }
            }
            // Folders that no longer exist drop out of the list.
            QDir(made[11]).removeRecursively();
            if (controller.recentFolders().contains(made[11])) { app.exit(364); return; }
            // The ribbon button opens a menu; choosing an entry reopens that folder.
            window->requestActivate();
            settle();
            QQuickItem *button = findVisual(window->contentItem(), QStringLiteral("recent-folders-button"));
            if (!button || !button->isEnabled()) { app.exit(365); return; }
            QMetaObject::invokeMethod(button, "clicked");
            settle();
            QObject *menu = root->findChild<QObject *>(QStringLiteral("recent-folders-menu"));
            if (!menu || !menu->property("visible").toBool()) { app.exit(366); return; }
            if (findVisual(window->contentItem(), QStringLiteral("recent-folder-9"))) { app.exit(369); return; }
            QQuickItem *entry = findVisual(window->contentItem(), QStringLiteral("recent-folder-3"));
            if (!entry) entry = menu->findChild<QQuickItem *>(QStringLiteral("recent-folder-3"));
            if (!entry) { app.exit(367); return; }
            const QString wanted = entry->property("folderPath").toString();
            QMetaObject::invokeMethod(entry, "triggered");
            settle();
            if (controller.projectFolder() != wanted || controller.recentFolders().first() != wanted) { app.exit(368); return; }
            app.exit(0);
        });
        return app.exec();
    }
    if (arguments.contains(QStringLiteral("--table-layout-check"))) {
        theme.setZoomPercent(80);
        QTimer::singleShot(500, &app, [&app, &engine] {
            auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
            auto frame = [&](int row, int col) -> QQuickItem * {
                return window ? findVisual(window->contentItem(),
                    QStringLiteral("table-frame-0-%1-%2").arg(row).arg(col)) : nullptr;
            };
            QQuickItem *wrapped = frame(1, 0);
            QQuickItem *shortCell = frame(1, 1);
            QQuickItem *nextRow = frame(2, 0);
            QQuickItem *header = frame(0, 0);
            if (!wrapped || !shortCell || !nextRow || !header) { app.exit(180); return; }
            const bool sameHeight = qAbs(wrapped->height() - shortCell->height()) < 1.0;
            const bool fullLine = qAbs(nextRow->y() - wrapped->y() - wrapped->height()) < 1.0;
            const bool wrappedRow = wrapped->height() > header->height() + 5;
            app.exit(sameHeight && fullLine && wrappedRow ? 0 : 181);
        });
        return app.exec();
    }
    if (arguments.contains(QStringLiteral("--interaction-check"))) {
        QTimer::singleShot(500, &app, [&app, &engine, &controller] {
            auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
            QObject *cell = window ? findVisual(window->contentItem(), QStringLiteral("table-cell-0-1-0")) : nullptr;
            if (!cell) {
                app.exit(30);
                return;
            }
            cell->setProperty("text", QStringLiteral("Changed via QML"));
            cell->setProperty("contentDirty", true);
            if (!QMetaObject::invokeMethod(cell, "commit")) {
                app.exit(31);
                return;
            }
            if (!controller.sourceText().contains(QStringLiteral("| Changed via QML | 12 | *review* |"))) {
                app.exit(32);
                return;
            }
            QCoreApplication::processEvents();
            QObject *formattedCell = findVisual(window->contentItem(), QStringLiteral("table-cell-0-1-2"));
            if (!formattedCell) {
                app.exit(33);
                return;
            }
            QString html = formattedCell->property("text").toString();
            html.replace(QStringLiteral("review"), QStringLiteral("approved"));
            formattedCell->setProperty("text", html);
            formattedCell->setProperty("contentDirty", true);
            if (!QMetaObject::invokeMethod(formattedCell, "commit")) {
                app.exit(34);
                return;
            }
            if (!controller.sourceText().contains(QStringLiteral("| Changed via QML | 12 | *approved* |"))) {
                app.exit(35);
                return;
            }
            QObject *first = findVisual(window->contentItem(), QStringLiteral("table-cell-0-1-0"));
            if (!first) { app.exit(36); return; }
            qobject_cast<QQuickItem *>(first)->forceActiveFocus();
            QKeyEvent tab(QEvent::KeyPress, Qt::Key_Tab, Qt::NoModifier);
            QCoreApplication::sendEvent(window, &tab);
            QCoreApplication::processEvents();
            QObject *next = findVisual(window->contentItem(), QStringLiteral("table-cell-0-1-1"));
            if (!next || !next->property("activeFocus").toBool()) { app.exit(37); return; }
            QKeyEvent down(QEvent::KeyPress, Qt::Key_Down, Qt::NoModifier);
            QCoreApplication::sendEvent(window, &down);
            QCoreApplication::processEvents();
            QObject *below = findVisual(window->contentItem(), QStringLiteral("table-cell-0-2-1"));
            if (!below || !below->property("activeFocus").toBool()) { app.exit(38); return; }
            QKeyEvent up(QEvent::KeyPress, Qt::Key_Up, Qt::NoModifier);
            QCoreApplication::sendEvent(window, &up);
            QCoreApplication::processEvents();
            next = findVisual(window->contentItem(), QStringLiteral("table-cell-0-1-1"));
            app.exit(next && next->property("activeFocus").toBool() ? 0 : 39);
        });
        return app.exec();
    }
    if (arguments.contains(QStringLiteral("--ui-check")))
        return 0;
    const int screenshotIndex = arguments.indexOf(QStringLiteral("--screenshot"));
    if (screenshotIndex >= 0 && screenshotIndex + 1 < arguments.size()) {
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        if (!window)
            return 2;
        const QString outputPath = arguments[screenshotIndex + 1];
        QTimer::singleShot(500, &app, [&app, window, outputPath] {
            window->grabWindow().save(outputPath);
            app.quit();
        });
        return app.exec();
    }
    return -1;
}
