#include "sourcevisual.h"
#include <QBuffer>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QTimer>
#include <QImageReader>
#include <QAbstractTextDocumentLayout>
#include <QDir>
#include <QTextDocument>
#include <QtMath>
#include <QTextFragment>
#include <QFontMetricsF>
#include <QGuiApplication>
#include <QClipboard>
#include <QSet>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QCoreApplication>
#include <QPointer>

extern "C" { char *kern_markdown_spans(const char *); void kern_free_string(char *); }

QString documentSource(const QTextDocument *doc)
{
    QString text=doc->toRawText();
    text.replace(QChar::ParagraphSeparator,QLatin1Char('\n'));
    for(auto block=doc->begin();block.isValid();block=block.next()) {
        for(auto it=block.begin();!it.atEnd();++it) {
            const auto fragment=it.fragment();
            if(fragment.charFormat().objectType()!=SoftBreakSpace::Type) continue;
            const auto format=fragment.charFormat();
            const QChar original=format.hasProperty(SoftBreakSpace::OriginalCharacter)
                ? QChar(format.intProperty(SoftBreakSpace::OriginalCharacter)) : QChar('\n');
            for(int i=fragment.position();i<fragment.position()+fragment.length();++i)
                if(text[i]==QChar::ObjectReplacementCharacter) text[i]=original;
        }
    }
    return text;
}

QSizeF SoftBreakSpace::intrinsicSize(QTextDocument *doc,int,const QTextFormat &format)
{
    if(format.hasProperty(OriginalCharacter) && format.intProperty(OriginalCharacter)!='\n') return {0,0};
    const QFont font=format.toCharFormat().font().resolve(doc->defaultFont());
    const QFontMetricsF metrics(font);
    return {metrics.horizontalAdvance(QLatin1Char(' ')), metrics.height()};
}

bool SourceVisual::eventFilter(QObject *watched,QEvent *event)
{
    if(dispatching || !preview) return false;
    bool edit=event->type()==QEvent::InputMethod;
    if(event->type()==QEvent::MouseButtonPress || event->type()==QEvent::MouseButtonRelease)
        edit=static_cast<QMouseEvent *>(event)->button()==Qt::MiddleButton;
    if(event->type()==QEvent::KeyPress) {
        const auto *key=static_cast<QKeyEvent *>(event);
        edit=key->matches(QKeySequence::Paste) || key->matches(QKeySequence::Cut)
            || key->key()==Qt::Key_Backspace || key->key()==Qt::Key_Delete
            || (!(key->modifiers()&(Qt::ControlModifier|Qt::AltModifier|Qt::MetaModifier)) && !key->text().isEmpty());
    }
    if(!edit) return false;
    // Ordinary Qt typing is not an edit block, so joinPreviousEditBlock cannot
    // attach a later projection to it. Give input and its projection one block.
    dispatching=true;
    const QPointer<SourceVisual> alive(this);
    const QString before=documentSource(document());
    int insertedAt=0,insertedCount=0;
    const auto changes=connect(document(),&QTextDocument::contentsChange,this,
        [&](int at,int,int added) { insertedAt=at; insertedCount=added; });
    QTextCursor cursor(document()); cursor.beginEditBlock();
    QCoreApplication::sendEvent(watched,event);
    // A key handler may commit and replace this editor (for example at a table).
    if(!alive) return true;
    cursor.endEditBlock();
    disconnect(changes);
    // Qt inherits the preceding character's format when inserting plain text.
    // A real U+FFFC pasted beside a soft break must remain a literal character.
    // Use Qt's precise changed range (not a text diff, which is ambiguous for
    // repeated adjacent characters) and keep this repair in the same undo step.
    if(insertedCount>0) {
        cursor.joinPreviousEditBlock();
        const QString raw=document()->toRawText();
        for(int i=insertedAt;i<qMin(insertedAt+insertedCount,int(raw.size()));++i) {
            if(raw[i]!=QChar::ObjectReplacementCharacter) continue;
            cursor.setPosition(i); cursor.setPosition(i+1,QTextCursor::KeepAnchor);
            if(cursor.charFormat().objectType()==SoftBreakSpace::Type) {
                QTextCharFormat format; format.setObjectType(QTextFormat::NoObject); cursor.mergeCharFormat(format);
            }
        }
        cursor.endEditBlock();
    }
    if(documentSource(document())!=before) watched->setProperty("contentDirty",true);
    dispatching=false;
    return true;
}

SourceVisual::SourceVisual(QTextDocument *doc) : QSyntaxHighlighter(doc)
{
    doc->setProperty("sourceSyntax",true);
    doc->documentLayout()->registerHandler(SoftBreakSpace::Type,new SoftBreakSpace(this));
    connect(doc,&QTextDocument::contentsChanged,this,[this] {
        if(refreshing || projecting || queued || documentSource(document())==source) return;
        queued=true;
        QTimer::singleShot(0,this,[this]{ queued=false; parse(); });
    });
}

SourceVisual *SourceVisual::get(QTextDocument *doc) { return doc->findChild<SourceVisual *>(QString(),Qt::FindDirectChildrenOnly); }

void SourceVisual::configure(QColor fg,QColor ac,QColor mu,QColor bg,QString ff,int px,QUrl base,qreal width)
{
    foreground=fg; accent=ac; muted=mu; background=bg; family=ff; size=px;
    baseUrl=base; availableWidth=qMax(40.,width); parse();
}

void SourceVisual::select(int a,int b,bool focused)
{
    if(projecting) return;
    if(!focused) { a=-1; b=-1; }
    if(start==a && end==b && source==documentSource(document())) return;
    start=a; end=b;
    if(start>=0 && end>start && QGuiApplication::clipboard()->supportsSelection()) {
        // Qt's primary selection otherwise exposes the inline object character.
        // Run after Qt publishes its default selection for this event.
        QTimer::singleShot(0,this,[this] {
            if(start>=0 && end>start)
                QGuiApplication::clipboard()->setText(documentSource(document()).mid(start,end-start),QClipboard::Selection);
        });
    }
    // Selection signals also fire midway through Qt's multi-command undo. Wait
    // for that transaction to finish before changing its display projection.
    if(source!=documentSource(document())) {
        if(!queued) { queued=true; QTimer::singleShot(0,this,[this]{ queued=false; parse(); }); }
    } else refresh();
}

bool SourceVisual::touches(int a,int b) const { return start>=0 && end>=a && start<=b; }

void SourceVisual::parse()
{
    if(projecting) return;
    source=documentSource(document());
    const auto bytes=source.toUtf8();
    char *json=kern_markdown_spans(bytes.constData());
    nodes=QJsonDocument::fromJson(json ? QByteArray(json) : QByteArray()).array();
    kern_free_string(json); projectSoftBreaks(); refresh();
}

void SourceVisual::projectSoftBreaks()
{
    QSet<int> breaks;
    if(preview) for(const auto &value:nodes) {
        const auto node=value.toObject();
        if(node["kind"]=="softbreak") {
            breaks.insert(node["from"].toInt());
            // Tabs use Qt's tab stops even with a zero-advance font. Project
            // continuation padding as zero-width objects and retain each source
            // character in its format, just as we retain the soft newline.
            for(const auto &mark:node["marks"].toArray()) {
                const auto range=mark.toArray();
                for(int i=range[0].toInt();i<range[1].toInt();++i) breaks.insert(i);
            }
        }
    }
    const QString raw=document()->toRawText();
    QVector<int> changes;
    for(int i=0;i<source.size();++i) {
        const bool projected=raw[i]==QChar::ObjectReplacementCharacter && source[i]!=QChar::ObjectReplacementCharacter;
        if(projected!=breaks.contains(i)) changes.append(i);
    }
    if(changes.isEmpty()) return;
    projecting=true;
    emit projectionStarting();
    const bool hadUndo=document()->isUndoAvailable();
    const bool modified=document()->isModified();
    QTextCursor cursor(document());
    // Include layout substitutions in the edit that changed their syntax.
    // Undo/redo therefore restore both text and its newline identities together.
    cursor.joinPreviousEditBlock();
    for(auto it=changes.crbegin();it!=changes.crend();++it) {
        cursor.setPosition(*it); cursor.setPosition(*it+1,QTextCursor::KeepAnchor);
        QTextCharFormat format;
        if(breaks.contains(*it)) {
            format.setObjectType(SoftBreakSpace::Type);
            format.setProperty(SoftBreakSpace::OriginalCharacter,source[*it].unicode());
            cursor.insertText(QString(QChar::ObjectReplacementCharacter),format);
        } else cursor.insertText(QString(source[*it]),format);
    }
    cursor.endEditBlock();
    if(!hadUndo) document()->clearUndoRedoStacks();
    document()->setModified(modified);
    emit projectionFinished();
    projecting=false;
}

void SourceVisual::copySource(int a,int b,bool cut)
{
    if(b<=a) return;
    QGuiApplication::clipboard()->setText(documentSource(document()).mid(a,b-a));
    if(cut) {
        QTextCursor cursor(document()); cursor.setPosition(a); cursor.setPosition(b,QTextCursor::KeepAnchor);
        cursor.removeSelectedText();
    }
}

static QString imageData(const QImage &image) {
    QByteArray data; QBuffer buffer(&data); buffer.open(QIODevice::WriteOnly); image.save(&buffer,"PNG");
    return QStringLiteral("data:image/png;base64,")+QString::fromLatin1(data.toBase64());
}

void SourceVisual::refresh()
{
    if(refreshing) return;
    refreshing=true; widgets.clear(); hidden.clear(); spaces.clear();
    for(auto block=document()->begin();block.isValid();block=block.next()) block.setVisible(true);
    if(!preview) { rehighlight(); refreshing=false; emit updated(); return; }
    for(const auto &value:nodes) {
        const auto n=value.toObject(); const QString kind=n["kind"].toString();
        const int a=n["from"].toInt(),b=n["to"].toInt();
        // Padding already has zero-width objects. Applying the marker-hiding
        // font on top would interfere with Qt's inline-object layout.
        if(kind=="softbreak") continue;
        if(kind=="codeblock" || kind=="html" || kind=="metadata" || kind=="quote") {
            // Inline HTML gets character styling, not a full-width panel.
            if(kind!="html" || source.mid(a,b-a).contains('\n'))
                widgets.append(QVariantMap{{"kind","background"},{"from",a},{"to",qMax(a,b-1)},
                    {"quote",kind=="quote"}});
        }
        if(touches(a,b)) continue;
        if(kind=="math" || kind=="displaymath" || kind=="image") {
            QImage image; QString error;
            if(kind=="image") {
                const QUrl url=baseUrl.resolved(QUrl(n["value"].toString()));
                if(url.isLocalFile()) {
                    QImageReader reader(url.toLocalFile());
                    const QSize dimensions=reader.size();
                    if(dimensions.isValid()) reader.setScaledSize(dimensions.scaled(QSize(qRound(availableWidth*2),640),Qt::KeepAspectRatio));
                    image=reader.read(); image.setDevicePixelRatio(2);
                }
            } else {
                const QString equation=n["value"].toString();
                const QString key=kind+QString::number(size)+foreground.name()+equation;
                if(!equationCache.contains(key)) equationCache.insert(key,new EquationImage(renderEquation(equation,kind=="displaymath",size+2,foreground)));
                image=equationCache[key]->image; error=equationCache[key]->error;
            }
            if(image.isNull()) {
                if(!error.isEmpty()) widgets.append(QVariantMap{{"kind","error"},{"from",a},{"to",b},{"message",error}});
                continue;
            }
            qreal w=image.width()/image.devicePixelRatio(),h=image.height()/image.devicePixelRatio();
            if(w>availableWidth) { h*=availableWidth/w; w=availableWidth; }
            const int width=qCeil(w),height=qCeil(h);
            hidden.append({a,b}); spaces.append({a,width,height});
            widgets.append(QVariantMap{{"kind",kind},{"from",a},{"to",b},{"width",width},{"height",height},
                {"image",imageData(image)}});
            // Only equation-internal lines collapse; the source and paragraph separators remain intact.
            const auto first=document()->findBlock(a);
            const auto last=document()->findBlock(qMax(a,b-1));
            for(auto block=first.next();block.isValid() && block.position()<=last.position();block=block.next()) {
                if(block.position()+block.text().size()<=b) block.setVisible(false);
            }
        } else if(kind=="task" || kind=="bullet") {
            int left=a,right=b;
            if(kind=="bullet") {
                const auto marks=n["marks"].toArray(); if(marks.isEmpty()) continue;
                left=marks.first().toArray()[0].toInt(); right=left+1;
            }
            hidden.append({left,right}); spaces.append({left,size,size});
            widgets.append(QVariantMap{{"kind",kind},{"from",left},{"to",right},
                {"checked",n["value"].toString()=="true"},{"width",size},{"height",size}});
        } else {
            for(const auto &m:n["marks"].toArray()) {
                const auto range=m.toArray(); hidden.append({range[0].toInt(),range[1].toInt()});
            }
        }
    }
    rehighlight();
    document()->markContentsDirty(0,document()->characterCount());
    refreshing=false; emit updated();
}

void SourceVisual::merge(int a,int b,const QTextCharFormat &f)
{
    a=qMax(a,currentBlock().position())-currentBlock().position();
    b=qMin(b,currentBlock().position()+currentBlock().text().size())-currentBlock().position();
    for(int i=a;i<b;++i) { auto merged=format(i); merged.merge(f); setFormat(i,1,merged); }
}

void SourceVisual::highlightBlock(const QString &text)
{
    const int a=currentBlock().position(),b=a+text.size();
    QColor shade=background;
    shade.setRgbF(background.redF()*.92+foreground.redF()*.08,background.greenF()*.92+foreground.greenF()*.08,background.blueF()*.92+foreground.blueF()*.08);
    for(const auto &v:nodes) {
        const auto n=v.toObject(); const int from=n["from"].toInt(),to=n["to"].toInt();
        if(to<=a || from>=b) continue;
        const QString kind=n["kind"].toString(); QTextCharFormat f;
        if(kind=="bold" || kind=="heading") f.setFontWeight(QFont::Bold);
        if(kind=="italic") f.setFontItalic(true);
        if(kind=="strike") f.setFontStrikeOut(true);
        if(kind=="heading") f.setProperty(QTextFormat::FontPixelSize,qRound(size*(1.6-.1*n["level"].toInt())));
        if(kind=="code" || kind=="codeblock" || kind=="html" || kind=="metadata") {
            f.setFontFamilies({family}); if(kind=="code") f.setBackground(shade);
        }
        if(kind=="link" || kind=="footnote") { f.setForeground(accent); f.setFontUnderline(true); }
        if(kind=="quote" || kind=="rule" || kind=="html" || kind=="metadata") f.setForeground(muted);
        if(kind=="math" || kind=="displaymath") f.setForeground(accent);
        merge(from,to,f);
        if(kind=="codeblock" || kind=="math" || kind=="displaymath" || kind=="html") {
            QTextCharFormat token; token.setForeground(accent);
            const QRegularExpression expression(kind=="math" || kind=="displaymath"
                ? QStringLiteral(R"(\\[A-Za-z]+|[{}_^])")
                : kind=="html" ? QStringLiteral(R"(</?[A-Za-z][\w:-]*|\b[\w:-]+(?=\s*=))")
                : QStringLiteral(R"(\b(?:if|else|for|while|return|class|struct|def|fn|let|const|var|import|from|include|public|private|true|false|null|None|True|False|int|float|double|void|auto)\b|\b[0-9]+(?:\.[0-9]+)?\b|"(?:[^"\\]|\\.)*"|'(?:[^'\\]|\\.)*')"));
            auto matches=expression.globalMatch(text);
            while(matches.hasNext()) { const auto m=matches.next(); merge(qMax(from,a+m.capturedStart()),qMin(to,a+m.capturedEnd()),token); }
        }
    }
    QTextCharFormat hide; QFont blank(QStringLiteral("Kern Hidden")); blank.setPixelSize(size);
    blank.setStyleStrategy(QFont::NoFontMerging); hide.setFont(blank); hide.setForeground(Qt::transparent);
    for(const auto &range:hidden) merge(range.first,range.second,hide);
    for(const auto &space:spaces) {
        QFont font(QStringLiteral("Kern Spacer")); font.setPixelSize(qMax(size,space.height));
        font.setStyleStrategy(QFont::NoFontMerging); font.setStretch(qBound(1,qRound(100.*space.width/qMax(size,space.height)),4000));
        QTextCharFormat f; f.setFont(font); f.setForeground(Qt::transparent); merge(space.from,space.from+1,f);
    }
}

void SourceVisual::activate(int a,int b,const QString &kind)
{
    if(kind!="task" || source.mid(a,b-a).size()!=3) return;
    QTextCursor cursor(document()); cursor.setPosition(a); cursor.setPosition(b,QTextCursor::KeepAnchor);
    cursor.insertText(source.mid(a,b-a)=="[ ]" ? "[x]" : "[ ]");
}

QString SourceVisual::linkAt(int position) const {
    for(const auto &v:nodes) { const auto n=v.toObject(); if(n["kind"]=="link" && position>=n["from"].toInt() && position<n["to"].toInt()) return n["value"].toString(); }
    return {};
}
bool SourceVisual::hiddenAt(int position) const { for(const auto &r:hidden) if(position>=r.first && position<r.second) return true; return false; }
bool SourceVisual::literalAt(int position) const {
    for(const auto &v:nodes) {
        const auto n=v.toObject(); const QString kind=n["kind"].toString();
        if((kind=="code" || kind=="codeblock" || kind=="html" || kind=="metadata" || kind=="math" || kind=="displaymath")
           && position>n["from"].toInt() && position<n["to"].toInt()) return true;
    }
    return false;
}
