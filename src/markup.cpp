#include "markup.h"

#include <QFont>
#include <QRegularExpression>
#include <QTextCharFormat>

namespace Markup {

QList<Span> spans(const QString &line) {
    // One to three stars, a word right after them, and the same stars
    // closing right after a word. No stars inside, so two marked stretches
    // on a line never swallow what stands between them.
    static const QRegularExpression marked(
        QStringLiteral("(?<!\\*)(\\*{1,3})(?=[^\\s*])([^*]+)(?<=[^\\s*])\\1(?!\\*)"));

    QList<Span> found;
    auto matches = marked.globalMatch(line);
    while (matches.hasNext()) {
        const auto match = matches.next();
        const int stars = match.capturedLength(1);
        const bool bold = stars >= 2;
        const bool italic = stars != 2;
        found << Span{int(match.capturedStart(1)), stars, bold, italic, true};
        found << Span{int(match.capturedStart(2)), int(match.capturedLength(2)), bold, italic, false};
        found << Span{int(match.capturedEnd(2)), stars, bold, italic, true};
    }
    return found;
}

QString html(const QString &line) {
    QString out;
    int at = 0;
    for (const Span &span : spans(line)) {
        out += line.mid(at, span.start - at).toHtmlEscaped();
        at = span.start + span.length;
        if (span.mark)
            continue;
        QString words = line.mid(span.start, span.length).toHtmlEscaped();
        if (span.italic)
            words = "<em>" + words + "</em>";
        if (span.bold)
            words = "<strong>" + words + "</strong>";
        out += words;
    }
    return out + line.mid(at).toHtmlEscaped();
}

} // namespace Markup

void MarkupHighlighter::setFaint(const QColor &faint) {
    if (m_faint == faint)
        return;
    m_faint = faint;
    rehighlight();
}

void MarkupHighlighter::highlightBlock(const QString &text) {
    for (const Markup::Span &span : Markup::spans(text)) {
        QTextCharFormat format;
        if (span.mark) {
            if (m_faint.isValid())
                format.setForeground(m_faint);
        } else {
            if (span.bold)
                format.setFontWeight(QFont::Bold);
            if (span.italic)
                format.setFontItalic(true);
        }
        setFormat(span.start, span.length, format);
    }
}
