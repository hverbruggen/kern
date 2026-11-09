#include "mathrenderer.h"
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QPainter>
#include <QRegularExpression>
#include "latex.h"
#include "core/formula.h"
#include "render.h"
#include "platform/qt/graphic_qt.h"

EquationImage renderEquation(const QString &source, bool display, int size, QColor color)
{
    // No document file access, external TeX process or shared macro definitions.
    if (source.size() > 16384) return {{}, QStringLiteral("Equation exceeds the preview size limit")};
    static const QRegularExpression unsafe(QStringLiteral(R"(\\(?:input|include|includegraphics|write|read|openout|openin|def|gdef|edef|xdef|newcommand|renewcommand|newenvironment|renewenvironment|DeclareMathOperator|DeclareMathSizes|usepackage|documentclass)\b)"));
    if (unsafe.match(source).hasMatch()) return {{}, QStringLiteral("Document commands and macro definitions are not supported in equation previews")};
    int nesting = 0;
    for (int i=0; i<source.size(); ++i) {
        if (source[i]=='\\') { ++i; continue; }
        if (source[i]=='{' && ++nesting > 128) return {{}, QStringLiteral("Equation nesting limit exceeded")};
        if (source[i]=='}' && --nesting < 0) return {{}, QStringLiteral("Unmatched closing brace")};
    }
    if (nesting) return {{}, QStringLiteral("Unclosed brace")};
    try {
        static bool initialized = false;
        if (!initialized) {
            const QDir app(QCoreApplication::applicationDirPath());
            QString resources = app.filePath(QStringLiteral("share/kern/microtex"));
            if (!QFileInfo::exists(resources)) resources = app.filePath(QStringLiteral("../share/kern/microtex"));
            if (!QFileInfo::exists(resources)) return {{}, QStringLiteral("Math fonts are missing from this installation")};
            tex::LaTeX::init(resources.toStdString()); initialized = true;
        }
        tex::Formula formula;
        tex::TeXParser parser(false,source.toStdWString(),&formula);
        parser.parse();
        tex::TeXRenderBuilder builder;
        std::unique_ptr<tex::TeXRender> render(builder.setStyle(display ? tex::TexStyle::display : tex::TexStyle::text)
            .setTextSize(size).setForeground(color.rgba()).build(formula));
        const int w=render->getWidth()+6, h=render->getHeight()+6;
        if (w<=0 || h<=0 || w>8192 || h>4096) return {{}, QStringLiteral("Equation exceeds preview dimensions")};
        QImage image(w*2,h*2,QImage::Format_ARGB32_Premultiplied);
        image.setDevicePixelRatio(2); image.fill(Qt::transparent);
        QPainter painter(&image); painter.setRenderHint(QPainter::Antialiasing);
        tex::Graphics2D_qt graphics(&painter); render->draw(graphics,3,3);
        return {image,{}};
    } catch (const std::exception &e) { return {{},QString::fromUtf8(e.what())}; }
    catch (...) { return {{},QStringLiteral("Unable to typeset this equation")}; }
}
