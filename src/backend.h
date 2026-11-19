#pragma once

#include <QObject>
#include <QString>
#include <QVariantList>

class QQuickTextDocument;
class QTextDocument;

// The shelf: every book is a folder under <root>/books/<id>/ holding a
// book.json that describes it and the book's own content beside it.
class Backend : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList books READ books NOTIFY booksChanged)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)

public:
    // An empty root means the user's own data folder; tests pass a temp one.
    explicit Backend(const QString &root = QString(), QObject *parent = nullptr);

    QVariantList books() const { return m_books; }
    QString error() const { return m_error; }
    QString root() const { return m_root; }

    // Returns the new book's id, or an empty string when it could not be saved.
    Q_INVOKABLE QString createBook(const QString &title, const QString &author);

    // The text of a book written here. An unknown book reads as empty and
    // sets the error.
    Q_INVOKABLE QString readText(const QString &id);
    Q_INVOKABLE bool saveText(const QString &id, const QString &text);
    // What the editor calls: saves the document's own characters, not the
    // flattened copy a text property hands out.
    Q_INVOKABLE bool saveDocument(const QString &id, QQuickTextDocument *document);

    // A document's text as it should be written to disk: only the paragraph
    // separators become newlines, so a non-breaking space the writer typed
    // is still one when the book is opened again.
    static QString rawText(const QTextDocument &document);

signals:
    void booksChanged();
    void errorChanged();

private:
    void load();
    // The book's content file, or an empty string when the shelf has no such
    // written book. An id is only ever matched against the shelf, never
    // turned into a path on its own.
    QString textPath(const QString &id) const;
    void setError(const QString &error);

    QString m_root;
    QVariantList m_books;
    QString m_error;
};
