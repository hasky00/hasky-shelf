#pragma once

#include <QColor>
#include <QList>
#include <QString>
#include <QSyntaxHighlighter>

// Bold and italic in a written book. The book stays plain text: the writer
// puts stars around the words, *like this* for italic, **like this** for
// bold and ***like this*** for both. The page shows them styled as they are
// typed, and a shared book carries them as real bold and italic.
namespace Markup {

// One stretch of a line: the stars themselves, or the words between them.
struct Span {
    int start = 0;
    int length = 0;
    bool bold = false;
    bool italic = false;
    // True for the stars, which the page dims and a shared book leaves out.
    bool mark = false;
};

// The styled stretches of one line, in order. Stars that hug no word on
// both sides ("2 * 3 * 4") are left as the stars they are, and nothing
// reaches past the end of the line.
QList<Span> spans(const QString &line);

// The line as XHTML: escaped, the stars gone, the words in <strong>/<em>.
QString html(const QString &line);

} // namespace Markup

// Styles the stars and their words on the page as the writer types. Only
// how the text looks changes; the text itself is never touched.
class MarkupHighlighter : public QSyntaxHighlighter {
    Q_OBJECT

public:
    explicit MarkupHighlighter(QTextDocument *document) : QSyntaxHighlighter(document) {}

    // The color the stars are dimmed to.
    void setFaint(const QColor &faint);

protected:
    void highlightBlock(const QString &text) override;

private:
    QColor m_faint;
};
