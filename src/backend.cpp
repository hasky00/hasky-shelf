#include "backend.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUuid>

#include <algorithm>

namespace {

// How many cover colors the window draws from the theme's palette.
constexpr int tints = 6;

// Written atomically, and never by truncating in place: a folder that can't
// take a temp file fails the save rather than risking the book.
bool save(const QString &path, const QByteArray &content) {
    QSaveFile file(path);
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly))
        return false;
    file.write(content);
    return file.commit();
}

} // namespace

Backend::Backend(const QString &root, QObject *parent)
    : QObject(parent),
      m_root(root.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) : root) {
    load();
}

void Backend::setError(const QString &error) {
    if (m_error == error)
        return;
    m_error = error;
    emit errorChanged();
}

void Backend::load() {
    QVariantList books;
    const QDir shelf(m_root + "/books");
    for (const QString &id : shelf.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        QFile file(shelf.filePath(id + "/book.json"));
        if (!file.open(QIODevice::ReadOnly))
            continue;
        const QJsonObject json = QJsonDocument::fromJson(file.readAll()).object();
        if (json.isEmpty())
            continue;
        QVariantMap book = json.toVariantMap();
        book["id"] = id;
        // The color is a property of the book, not of its place on the
        // shelf, so it stays the same as books come and go around it.
        book["tint"] = int(qHash(id) % tints);
        books << book;
    }
    // Oldest first: a new book lands at the end of the shelf.
    std::stable_sort(books.begin(), books.end(), [](const QVariant &a, const QVariant &b) {
        return a.toMap().value("created").toString() < b.toMap().value("created").toString();
    });
    m_books = books;
    emit booksChanged();
}

QString Backend::createBook(const QString &title, const QString &author) {
    const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QString folder = m_root + "/books/" + id;
    if (!QDir().mkpath(folder)) {
        setError(tr("The shelf folder could not be created."));
        return {};
    }

    QJsonObject json;
    json["title"] = title.trimmed().isEmpty() ? tr("Untitled") : title.trimmed();
    json["author"] = author.trimmed();
    json["kind"] = "written";
    json["file"] = "book.md";
    json["created"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);

    if (!save(folder + "/book.md", QByteArray()) || !save(folder + "/book.json", QJsonDocument(json).toJson())) {
        QDir(folder).removeRecursively();
        setError(tr("The book could not be saved."));
        return {};
    }

    setError({});
    load();
    return id;
}
