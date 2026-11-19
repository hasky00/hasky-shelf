#pragma once

#include <QObject>
#include <QString>
#include <QVariantList>

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

signals:
    void booksChanged();
    void errorChanged();

private:
    void load();
    void setError(const QString &error);

    QString m_root;
    QVariantList m_books;
    QString m_error;
};
