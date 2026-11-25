#include <QDir>
#include <QFile>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalServer>
#include <QLocalSocket>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QRegularExpression>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTextBlock>
#include <QTextDocument>
#include <QTextLayout>
#include <QtTest>

#include "backend.h"
#include "epub.h"
#include "filepicker.h"
#include "markup.h"
#include "theme.h"
#include "translator.h"

namespace {

bool write(const QString &path, const QByteArray &contents) {
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(contents) == contents.size();
}

int lines(const QString &path) {
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll().count('\n') : 0;
}

// The window's item with this objectName. What a Repeater, ListView or Loader
// makes isn't a QObject child of the window, so findChild never finds it; walk
// the items instead. Popups aren't items: find those with findChild.
QQuickItem *item(QQuickItem *from, const QString &name) {
    for (auto *child : from->childItems()) {
        if (child->objectName() == name)
            return child;
        if (auto *found = item(child, name))
            return found;
    }
    return nullptr;
}

// A hyprctl that answers with response.json beside it and counts its calls.
QString fakeHyprctl(const QString &directory) {
    const QString command = directory + "/hyprctl";
    write(command, "#!/bin/bash\necho >> '" + directory.toUtf8() + "/calls'\ncat '" + directory.toUtf8()
                       + "/response.json'\n");
    QFile::setPermissions(command, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
    return command;
}

QJsonObject readCatalog(const QString &path) {
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(file.readAll()).object() : QJsonObject();
}

// The placeholders in a text, sorted: %1 to %99, and the count (%n, %Ln).
QStringList placeholders(const QString &text) {
    static const QRegularExpression placeholder("%L?(?:[1-9][0-9]?|n)");
    QStringList found;
    for (auto it = placeholder.globalMatch(text); it.hasNext();)
        found << it.next().captured();
    found.sort();
    return found;
}

// Every text the sources hand to qsTr() or tr(), and whether a call passes it
// a count. It walks the code a token at a time, so a string never hides a
// comment or the other way round, C++'s adjacent literals join, and QML's
// single quotes count. A text that isn't a literal can't be in a catalog.
QMap<QString, bool> translatables(const QString &folder, QStringList *problems) {
    QMap<QString, bool> found;
    for (const QFileInfo &info : QDir(folder).entryInfoList({"*.qml", "*.js", "*.cpp", "*.h"}, QDir::Files)) {
        QFile file(info.filePath());
        if (!file.open(QIODevice::ReadOnly))
            continue;
        const QString code = QString::fromUtf8(file.readAll());
        qsizetype i = 0;
        auto literal = [&](QString *text) {
            const QChar quote = code[i++];
            for (; i < code.size() && code[i] != quote; ++i) {
                if (code[i] == '\\' && i + 1 < code.size()) {
                    const QChar escaped = code[++i];
                    const int digits = escaped == 'u' ? 4 : escaped == 'x' ? 2 : 0;
                    bool hex = false;
                    const ushort unit = code.mid(i + 1, digits).toUShort(&hex, 16);
                    if (digits && hex) {
                        *text += QChar(unit);
                        i += digits;
                    } else {
                        *text += escaped == 'n' ? QChar('\n') : escaped == 't' ? QChar('\t') : escaped;
                    }
                } else {
                    *text += code[i];
                }
            }
            ++i;
        };
        auto skipSpace = [&] {
            while (i < code.size() && code[i].isSpace())
                ++i;
        };
        while (i < code.size()) {
            const QChar c = code[i];
            if (code.mid(i, 2) == "//") {
                i = code.indexOf('\n', i);
                if (i < 0)
                    break;
            } else if (code.mid(i, 2) == "/*") {
                i = code.indexOf("*/", i + 2);
                if (i < 0)
                    break;
                i += 2;
            } else if (c == '"' || c == '\'') {
                QString skipped;
                literal(&skipped);
            } else if (c.isLetter() || c == '_') {
                const qsizetype start = i;
                while (i < code.size() && (code[i].isLetterOrNumber() || code[i] == '_'))
                    ++i;
                const QString word = code.mid(start, i - start);
                skipSpace();
                if ((word != "qsTr" && word != "tr") || i >= code.size() || code[i] != '(')
                    continue;
                ++i;
                skipSpace();
                if (i >= code.size() || (code[i] != '"' && code[i] != '\'')) {
                    *problems << info.fileName() + ": not a literal: " + code.mid(start, 40);
                    continue;
                }
                QString text;
                do {
                    literal(&text);
                    skipSpace();
                } while (i < code.size() && code[i] == '"');
                // The arguments after the text, to the call's closing parenthesis.
                int depth = 0, commas = 0;
                while (i < code.size()) {
                    const QChar d = code[i];
                    if (d == '"' || d == '\'') {
                        QString skipped;
                        literal(&skipped);
                        continue;
                    }
                    if (d == '(' || d == '[' || d == '{')
                        ++depth;
                    else if ((d == ')' || d == ']' || d == '}') && depth-- == 0)
                        break;
                    else if (d == ',' && depth == 0)
                        ++commas;
                    ++i;
                }
                found[text] = found.value(text) || commas >= 2;
            } else {
                ++i;
            }
        }
    }
    return found;
}

// Stands in for the desktop's file chooser: counts the asks, and answers
// with whatever the test says was picked.
class FakePicker : public FilePicker {
public:
    void openBooks() override {
        ++asked;
        if (!picked.isEmpty())
            emit booksSelected(picked);
    }
    void openCover() override {
        ++askedForCover;
        if (!pickedCover.isEmpty())
            emit coverSelected(pickedCover);
    }
    int asked = 0;
    int askedForCover = 0;
    QList<QUrl> picked;
    QUrl pickedCover;
};

// A real picture file, small enough to make in a test.
bool paint(const QString &path, const QColor &color) {
    QImage image(40, 60, QImage::Format_RGB32);
    image.fill(color);
    return image.save(path);
}

} // namespace

class HaskyShelfTests : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() { QQuickStyle::setStyle("Material"); }

    void aNewBookLandsAtTheEndOfTheShelf() {
        QTemporaryDir dir;
        Backend backend(dir.path());
        QVERIFY(backend.books().isEmpty());

        const QString first = backend.createBook("  Dune Sea  ", "A. Writer");
        QVERIFY(!first.isEmpty());
        const QString second = backend.createBook("", "");
        QVERIFY(!second.isEmpty());

        QCOMPARE(backend.books().size(), 2);
        const QVariantMap book = backend.books().at(0).toMap();
        QCOMPARE(book.value("id").toString(), first);
        QCOMPARE(book.value("title").toString(), QString("Dune Sea"));
        QCOMPARE(book.value("author").toString(), QString("A. Writer"));
        QCOMPARE(book.value("kind").toString(), QString("written"));
        // A book with no title still has a name to show on its cover.
        QCOMPARE(backend.books().at(1).toMap().value("title").toString(), QString("Untitled"));
        QVERIFY(QFile::exists(dir.path() + "/books/" + first + "/book.md"));
    }

    void theShelfIsReadBackFromDisk() {
        QTemporaryDir dir;
        QString id;
        {
            Backend backend(dir.path());
            id = backend.createBook("Kept", "Someone");
        }
        Backend reopened(dir.path());
        QCOMPARE(reopened.books().size(), 1);
        const QVariantMap book = reopened.books().at(0).toMap();
        QCOMPARE(book.value("id").toString(), id);
        QCOMPARE(book.value("title").toString(), QString("Kept"));
        // The cover color belongs to the book, so it is the same every time.
        Backend again(dir.path());
        QCOMPARE(again.books().at(0).toMap().value("tint"), book.value("tint"));
    }

    void aBookThatCannotBeSavedSaysSo() {
        QTemporaryDir dir;
        // A file where the shelf's folder should be: nothing can be made in it.
        QVERIFY(write(dir.path() + "/books", "in the way"));
        Backend backend(dir.path());
        QVERIFY(backend.createBook("Lost", "").isEmpty());
        QVERIFY(!backend.error().isEmpty());
        QVERIFY(backend.books().isEmpty());
    }

    void aBooksTextComesBackAsWritten() {
        QTemporaryDir dir;
        Backend backend(dir.path());
        const QString id = backend.createBook("Draft", "");
        QCOMPARE(backend.readText(id), QString());

        QVERIFY(backend.saveText(id, QString::fromUtf8("First line\nSecond — line")));
        QVERIFY(backend.error().isEmpty());
        Backend reopened(dir.path());
        QCOMPARE(reopened.readText(id), QString::fromUtf8("First line\nSecond — line"));
    }

    void aBookThatIsNotOnTheShelfIsNeitherReadNorWritten() {
        QTemporaryDir dir;
        Backend backend(dir.path());
        backend.createBook("Real", "");
        // An id is matched against the shelf, never followed as a path.
        QVERIFY(!backend.saveText("../outside", "text"));
        QVERIFY(!backend.error().isEmpty());
        QVERIFY(!QFile::exists(dir.path() + "/outside/book.md"));
        QCOMPARE(backend.readText("missing"), QString());
        QVERIFY(!backend.error().isEmpty());
    }

    void savingKeepsTheCharactersTheWriterTyped() {
        // A text property would hand back a plain space for the non-breaking
        // one, and the book would change by being opened and saved.
        QTextDocument document;
        document.setPlainText(QString::fromUtf8("10\u00a0km\nnext"));
        QCOMPARE(Backend::rawText(document), QString::fromUtf8("10\u00a0km\nnext"));
    }

    void aFileIsCopiedOntoTheShelf() {
        QTemporaryDir dir;
        const QString original = dir.path() + "/paper_satellites-v2.PDF";
        QVERIFY(write(original, "%PDF-1.7 not really"));
        Backend backend(dir.path() + "/shelf");
        QSignalSpy added(&backend, &Backend::added);

        backend.addFiles({QUrl::fromLocalFile(original)});
        QVERIFY(backend.busy());
        QTRY_COMPARE(added.count(), 1);
        QVERIFY(!backend.busy());

        QCOMPARE(backend.books().size(), 1);
        const QVariantMap book = backend.books().at(0).toMap();
        QCOMPARE(book.value("title").toString(), QString("paper satellites v2"));
        QCOMPARE(book.value("kind").toString(), QString("file"));
        QCOMPARE(book.value("format").toString(), QString("pdf"));
        const QString copy = backend.filePath(book.value("id").toString());
        QVERIFY(copy.startsWith(dir.path() + "/shelf/books/"));
        QFile shelved(copy);
        QVERIFY(shelved.open(QIODevice::ReadOnly));
        QCOMPARE(shelved.readAll(), QByteArray("%PDF-1.7 not really"));
        // The original is only read: it is still where it was.
        QVERIFY(QFile::exists(original));
        // A file book has no page to write on.
        QVERIFY(!backend.saveText(book.value("id").toString(), "text"));
    }

    void onlyBooksGoOnTheShelf() {
        QTemporaryDir dir;
        QVERIFY(write(dir.path() + "/notes.txt", "plain"));
        Backend backend(dir.path() + "/shelf");
        backend.addFiles({QUrl::fromLocalFile(dir.path() + "/notes.txt"), QUrl::fromLocalFile(dir.path() + "/gone.pdf")});
        QVERIFY(!backend.busy());
        QVERIFY(!backend.error().isEmpty());
        QVERIFY(backend.books().isEmpty());
    }

    void theChooserPutsItsPicksOnTheShelf() {
        QTemporaryDir dir;
        QVERIFY(write(dir.path() + "/one.epub", "PK"));
        FakePicker picker;
        picker.picked = {QUrl::fromLocalFile(dir.path() + "/one.epub")};
        Backend backend(dir.path() + "/shelf", &picker);
        backend.chooseFiles();
        QCOMPARE(picker.asked, 1);
        QTRY_COMPARE(backend.books().size(), 1);
        QCOMPARE(backend.books().at(0).toMap().value("format").toString(), QString("epub"));

        // Without a chooser the shelf says so rather than doing nothing.
        Backend alone(dir.path() + "/other");
        alone.chooseFiles();
        QVERIFY(!alone.error().isEmpty());
    }

    void aPictureStandsOnTheCover() {
        QTemporaryDir dir;
        const QString roses = dir.path() + "/roses.PNG";
        QVERIFY(paint(roses, Qt::red));
        Backend backend(dir.path() + "/shelf");
        const QString id = backend.createBook("Roses", "hasky");
        QCOMPARE(backend.books().at(0).toMap().value("coverUrl").toString(), QString());

        QVERIFY(backend.setCover(id, QUrl::fromLocalFile(roses)));
        QVERIFY(backend.error().isEmpty());
        const QString first = QUrl(backend.books().at(0).toMap().value("coverUrl").toString()).toLocalFile();
        QVERIFY(first.startsWith(dir.path() + "/shelf/books/" + id + "/cover-"));
        QCOMPARE(QImage(first).pixelColor(0, 0), QColor(Qt::red));
        // The original is only read, and the book is still the book it was.
        QVERIFY(QFile::exists(roses));
        QCOMPARE(backend.books().at(0).toMap().value("title").toString(), QString("Roses"));
        QCOMPARE(backend.books().at(0).toMap().value("author").toString(), QString("hasky"));

        // The cover is still there when the shelf is opened again.
        Backend reopened(dir.path() + "/shelf");
        QCOMPARE(QUrl(reopened.books().at(0).toMap().value("coverUrl").toString()).toLocalFile(), first);

        // A second picture takes the first one's place, under a new name.
        const QString other = dir.path() + "/other.jpg";
        QVERIFY(paint(other, Qt::blue));
        QTest::qWait(5);
        QVERIFY(backend.setCover(id, QUrl::fromLocalFile(other)));
        const QString second = QUrl(backend.books().at(0).toMap().value("coverUrl").toString()).toLocalFile();
        QVERIFY(second != first);
        QVERIFY(second.endsWith(".jpg"));
        QVERIFY(!QFile::exists(first));
        QVERIFY(QFile::exists(second));
    }

    void onlyPicturesGoOnACover() {
        QTemporaryDir dir;
        Backend backend(dir.path() + "/shelf");
        const QString id = backend.createBook("Plain", "");
        // The name says picture but the content is not one.
        QVERIFY(write(dir.path() + "/fake.png", "not a picture"));
        QVERIFY(!backend.setCover(id, QUrl::fromLocalFile(dir.path() + "/fake.png")));
        QVERIFY(!backend.error().isEmpty());
        QVERIFY(write(dir.path() + "/notes.txt", "plain"));
        QVERIFY(!backend.setCover(id, QUrl::fromLocalFile(dir.path() + "/notes.txt")));
        QVERIFY(!backend.setCover(id, QUrl::fromLocalFile(dir.path() + "/gone.png")));
        QCOMPARE(backend.books().at(0).toMap().value("coverUrl").toString(), QString());
        QCOMPARE(QDir(dir.path() + "/shelf/books/" + id).entryList(QDir::Files).size(), 2);

        // A book that is not on the shelf gets no cover, and no folder.
        QVERIFY(paint(dir.path() + "/real.png", Qt::green));
        QVERIFY(!backend.setCover("../outside", QUrl::fromLocalFile(dir.path() + "/real.png")));
        QVERIFY(!QDir(dir.path() + "/shelf/outside").exists());

        QVERIFY(backend.isPicture(QUrl::fromLocalFile("/somewhere/Rose.JPEG")));
        QVERIFY(!backend.isPicture(QUrl::fromLocalFile("/somewhere/rose.pdf")));
    }

    void aCoverNamedInTheDescriptionIsNeverFollowedAsAPath() {
        QTemporaryDir dir;
        QVERIFY(paint(dir.path() + "/secret.png", Qt::red));
        QString id;
        {
            Backend backend(dir.path() + "/shelf");
            id = backend.createBook("Sly", "");
        }
        const QString description = dir.path() + "/shelf/books/" + id + "/book.json";
        QFile file(description);
        QVERIFY(file.open(QIODevice::ReadOnly));
        QJsonObject json = QJsonDocument::fromJson(file.readAll()).object();
        file.close();
        json["cover"] = "../../../secret.png";
        QVERIFY(write(description, QJsonDocument(json).toJson()));
        Backend backend(dir.path() + "/shelf");
        QCOMPARE(backend.books().at(0).toMap().value("coverUrl").toString(), QString());
    }

    void theChooserPutsItsPictureOnTheBook() {
        QTemporaryDir dir;
        QVERIFY(paint(dir.path() + "/roses.png", Qt::red));
        FakePicker picker;
        picker.pickedCover = QUrl::fromLocalFile(dir.path() + "/roses.png");
        Backend backend(dir.path() + "/shelf", &picker);
        const QString first = backend.createBook("One", "");
        const QString second = backend.createBook("Two", "");
        backend.chooseCover(second);
        QCOMPARE(picker.askedForCover, 1);
        // The picture goes on the book it was picked for, and only that one.
        for (const QVariant &entry : backend.books()) {
            const QVariantMap book = entry.toMap();
            QCOMPARE(book.value("coverUrl").toString().isEmpty(), book.value("id").toString() == first);
        }
        // A book that is not on the shelf never opens the chooser.
        backend.chooseCover("missing");
        QCOMPARE(picker.askedForCover, 1);

        Backend alone(dir.path() + "/other");
        alone.chooseCover(alone.createBook("Solo", ""));
        QVERIFY(!alone.error().isEmpty());
    }

    void starredWordsAreBoldAndItalic() {
        QCOMPARE(Markup::html("a *soft* word"), QString("a <em>soft</em> word"));
        QCOMPARE(Markup::html("a **loud** word"), QString("a <strong>loud</strong> word"));
        QCOMPARE(Markup::html("***both***"), QString("<strong><em>both</em></strong>"));
        QCOMPARE(Markup::html("*one* and **two** here"), QString("<em>one</em> and <strong>two</strong> here"));
        QCOMPARE(Markup::html("**two words**, then *more of them*."),
                 QString("<strong>two words</strong>, then <em>more of them</em>."));
        // What is inside is still escaped.
        QCOMPARE(Markup::html("*a < b* & c"), QString("<em>a &lt; b</em> &amp; c"));
        // Stars that hug no word are the stars they are.
        QCOMPARE(Markup::html("2 * 3 * 4"), QString("2 * 3 * 4"));
        QCOMPARE(Markup::html("a lone * star"), QString("a lone * star"));
        QCOMPARE(Markup::html("* not this *"), QString("* not this *"));
        QCOMPARE(Markup::html("**open and never closed"), QString("**open and never closed"));
        QCOMPARE(Markup::html("**uneven*"), QString("**uneven*"));
        QCOMPARE(Markup::html("****"), QString("****"));
        QCOMPARE(Markup::html(""), QString());

        // The page dims the stars and styles the words between them.
        const QList<Markup::Span> spans = Markup::spans("x **yo** z");
        QCOMPARE(spans.size(), 3);
        QVERIFY(spans[0].mark && spans[0].start == 2 && spans[0].length == 2);
        QVERIFY(!spans[1].mark && spans[1].bold && !spans[1].italic && spans[1].start == 4 && spans[1].length == 2);
        QVERIFY(spans[2].mark && spans[2].start == 6 && spans[2].length == 2);

        // A shared book carries them, and nothing reaches across a line.
        const QString page = Epub::page("T", "", "a **bold** line\nan *open one\nclosed* here");
        QVERIFY(page.contains("a <strong>bold</strong> line"));
        QVERIFY(page.contains("an *open one"));
        QVERIFY(page.contains("closed* here"));
    }

    void thePageStylesStarredWordsWithoutChangingThem() {
        QTextDocument document;
        document.setPlainText("plain **bold** and *soft*");
        auto *highlighter = new MarkupHighlighter(&document);
        highlighter->setFaint(QColor(Qt::gray));
        QCoreApplication::processEvents();
        bool bold = false, italic = false;
        for (const auto &range : document.firstBlock().layout()->formats()) {
            if (range.start == 8 && range.length == 4)
                bold = range.format.fontWeight() == QFont::Bold;
            if (range.start == 20 && range.length == 4)
                italic = range.format.fontItalic();
        }
        QVERIFY(bold);
        QVERIFY(italic);
        // Only the look changed: what is saved is what was typed.
        QCOMPARE(Backend::rawText(document), QString("plain **bold** and *soft*"));
    }

    void aSharedBookKeepsItsLinesAndItsMarks() {
        const QString page = Epub::page("Salt & Tide", "A <Writer>", "one\ntwo\n\n\nthree < four\r\n");
        QVERIFY(page.contains("<h1>Salt &amp; Tide</h1>"));
        QVERIFY(page.contains("A &lt;Writer&gt;"));
        // Verse keeps its line breaks; a blank line starts a new paragraph.
        QVERIFY(page.contains(QRegularExpression("<p>one<br/>\\s+two</p>")));
        QVERIFY(page.contains("<p>three &lt; four</p>"));
        QCOMPARE(page.count("<p>"), 2);
    }

    void aWrittenBookIsSharedAsAnEpub() {
        QTemporaryDir dir;
        Backend backend(dir.path() + "/shelf");
        backend.setShareFolder(dir.path() + "/out");
        const QString id = backend.createBook("Salt / Tide", "Mara");
        QVERIFY(backend.saveText(id, "first line\nsecond line"));
        QSignalSpy shared(&backend, &Backend::shared);

        backend.shareBook(id);
        QTRY_COMPARE(shared.count(), 1);
        // The title's slash never becomes a folder.
        const QString path = dir.path() + "/out/Salt Tide.epub";
        QCOMPARE(shared.at(0).at(0).toString(), path);
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QByteArray bytes = file.readAll();
        // What makes a reader take it for an EPUB: a ZIP whose first file
        // is the stored mimetype.
        QVERIFY(bytes.startsWith("PK\x03\x04"));
        QCOMPARE(bytes.mid(30, 28), QByteArray("mimetypeapplication/epub+zip"));
        QVERIFY(bytes.contains("first line<br/>"));
        QVERIFY(bytes.contains("<dc:creator>Mara</dc:creator>"));

        // Sharing again never writes over the copy made before.
        backend.shareBook(id);
        QTRY_COMPARE(shared.count(), 2);
        QCOMPARE(shared.at(1).at(0).toString(), dir.path() + "/out/Salt Tide 2.epub");

        // A book that isn't on the shelf shares nothing.
        backend.shareBook("../../etc");
        QVERIFY(!backend.error().isEmpty());
        QCOMPARE(QDir(dir.path() + "/out").entryList(QDir::Files).size(), 2);
    }

    void anAddedBookIsSharedAsTheFileItIs() {
        QTemporaryDir dir;
        QVERIFY(write(dir.path() + "/glass.pdf", "%PDF-1.7 not really"));
        Backend backend(dir.path() + "/shelf");
        backend.setShareFolder(dir.path() + "/out");
        QSignalSpy added(&backend, &Backend::added);
        backend.addFiles({QUrl::fromLocalFile(dir.path() + "/glass.pdf")});
        QTRY_COMPARE(added.count(), 1);
        QSignalSpy shared(&backend, &Backend::shared);

        backend.shareBook(added.at(0).at(0).toString());
        QTRY_COMPARE(shared.count(), 1);
        QFile copy(dir.path() + "/out/glass.pdf");
        QVERIFY(copy.open(QIODevice::ReadOnly));
        QCOMPARE(copy.readAll(), QByteArray("%PDF-1.7 not really"));
    }

    void themeFallsBack() {
        QTemporaryDir dir;
        Theme theme(dir.path());
        QCOMPARE(theme.accent(), QColor("#FFD60A"));
        QCOMPARE(theme.accentForeground(), QColor("black"));
        QVERIFY(!theme.light());
        QCOMPARE(theme.radius(), 0);
    }

    void themeReadsThePalette() {
        QTemporaryDir dir;
        QVERIFY(QDir(dir.path()).mkpath("theme"));
        QVERIFY(write(dir.path() + "/theme/colors.toml", "mode = \"light\"\n"
                                                         "theme_type = \"dark\"\n"
                                                         "accent = \"#112233\"\n"
                                                         "bg = #fafafa # paper\n"
                                                         "foreground = '#202020'\n"
                                                         "color1 = \"#aa0000\"\n"));

        Theme theme(dir.path());
        QCOMPARE(theme.accent(), QColor("#112233"));
        QCOMPARE(theme.accentForeground(), QColor("white"));
        QCOMPARE(theme.background(), QColor("#fafafa"));
        QCOMPARE(theme.foreground(), QColor("#202020"));
        QCOMPARE(theme.colors().value("red").value<QColor>(), QColor("#aa0000"));
        QVERIFY(theme.light());
    }

    void themeAsksHyprlandAgainOnlyWhenItReloads() {
        // The fake Hyprland lives outside the theme directory, whose changes
        // the Theme also follows.
        QTemporaryDir dir, hypr;
        const QString command = fakeHyprctl(hypr.path());
        QVERIFY(write(hypr.filePath("response.json"), "{\"int\":4}"));
        QLocalServer events;
        QVERIFY(events.listen(hypr.filePath("events.sock")));

        // Once on start, and once more when the event socket connects.
        Theme theme(dir.path(), nullptr, command, events.fullServerName());
        QTRY_VERIFY(events.hasPendingConnections());
        QLocalSocket *hyprland = events.nextPendingConnection();
        QTRY_COMPARE(lines(hypr.filePath("calls")), 2);
        QTRY_COMPARE(theme.radius(), 4);

        QVERIFY(write(hypr.filePath("response.json"), "{\"int\":10}"));
        QTest::qWait(300);
        QCOMPARE(lines(hypr.filePath("calls")), 2);
        // Events arrive in pieces; only a whole configreloaded line counts.
        hyprland->write("activewindow>>kitty,~\nconfigrel");
        hyprland->flush();
        QTest::qWait(100);
        QCOMPARE(lines(hypr.filePath("calls")), 2);
        hyprland->write("oaded>>\n");
        QTRY_COMPARE(theme.radius(), 10);
        QCOMPARE(lines(hypr.filePath("calls")), 3);
    }

    void themeReachesHyprlandOnceItListens() {
        QTemporaryDir dir, hypr;
        const QString command = fakeHyprctl(hypr.path());
        QVERIFY(write(hypr.filePath("response.json"), "{\"int\":4}"));
        Theme theme(dir.path(), nullptr, command, hypr.filePath("events.sock"));
        QTRY_COMPARE(theme.radius(), 4);

        QVERIFY(write(hypr.filePath("response.json"), "{\"int\":8}"));
        QLocalServer events;
        QVERIFY(events.listen(hypr.filePath("events.sock")));
        QTRY_VERIFY_WITH_TIMEOUT(events.hasPendingConnections(), 5000);
        QTRY_COMPARE(theme.radius(), 8);
    }

    void translatorFallsBackToEnglish() {
        QTemporaryDir dir;
        Translator translator(dir.path() + "/language", dir.path());
        QCOMPARE(translator.language(), QString("en"));
        QVERIFY(translator.translate("Main", "Save").isNull());
    }

    void translatorFollowsTheLanguageFile() {
        QTemporaryDir dir;
        QVERIFY(write(dir.path() + "/language", "pt-BR\n"));
        QVERIFY(write(dir.path() + "/pt.json", R"({"Save": "Guardar", "Open": "Abrir"})"));
        QVERIFY(write(dir.path() + "/pt-BR.json", R"({"Save": "Salvar"})"));

        Translator translator(dir.path() + "/language", dir.path());
        QCOMPARE(translator.language(), QString("pt-BR"));
        QCOMPARE(translator.translate("Main", "Save"), QString("Salvar"));
        QCOMPARE(translator.translate("Main", "Open"), QString("Abrir"));
        QVERIFY(translator.translate("Main", "Quit").isNull());
    }

    void translatorSeesAFirstPick() {
        // Before the first pick there is no ~/.config/omarchy to watch.
        QTemporaryDir dir;
        QVERIFY(write(dir.path() + "/da.json", R"({"Save": "Gem"})"));
        Translator translator(dir.path() + "/omarchy/language", dir.path());
        QVERIFY(QDir(dir.path()).mkpath("omarchy"));
        QVERIFY(write(dir.path() + "/omarchy/language", "da\n"));
        QTRY_COMPARE(translator.translate("Main", "Save"), QString("Gem"));
    }

    void translatorPicksPluralForms() {
        QTemporaryDir dir;
        QVERIFY(write(dir.path() + "/language", "ru\n"));
        QVERIFY(write(dir.path() + "/en.json", R"({"%n clips": {"one": "%n clip", "other": "%n clips"},
                                                   "%n files": {"one": "%n file", "other": "%n files"}})"));
        QVERIFY(write(dir.path() + "/ru.json", R"({"%n clips": {"one": "%n клип", "few": "%n клипа", "many": "%n клипов", "other": "%n клипа"}})"));

        Translator translator(dir.path() + "/language", dir.path());
        QCOMPARE(translator.translate("Main", "%n clips", nullptr, 21), QString("%n клип"));
        QCOMPARE(translator.translate("Main", "%n clips", nullptr, 22), QString("%n клипа"));
        QCOMPARE(translator.translate("Main", "%n clips", nullptr, 25), QString("%n клипов"));
        // An untranslated counted string falls back to English, with English rules.
        QCOMPARE(translator.translate("Main", "%n files", nullptr, 21), QString("%n files"));
    }

    void englishCatalogListsEveryString() {
        // en.json is what translators work from: every text the sources
        // translate is in it, as itself or, when counted, as its English
        // plural forms, and nothing else is.
        const QString sources = QFileInfo(QFINDTESTDATA("../src/Main.qml")).absolutePath();
        QStringList problems;
        const QMap<QString, bool> used = translatables(sources, &problems);
        const QJsonObject english = readCatalog(sources + "/i18n/en.json");
        for (auto it = used.begin(); it != used.end(); ++it) {
            const QJsonValue value = english.value(it.key());
            if (value.isUndefined())
                problems << "missing from en.json: " + it.key();
            else if (it.value() && (value.toObject().keys() != QStringList{"one", "other"}
                                    || value.toObject().value("other").toString() != it.key()))
                problems << "counted, so needs its one and other forms: " + it.key();
            else if (!it.value() && value.toString() != it.key())
                problems << "should be itself: " + it.key();
        }
        for (const QString &key : english.keys())
            if (!used.contains(key))
                problems << "in en.json but no source translates it: " + key;
        QVERIFY2(problems.isEmpty(), qPrintable(problems.join('\n')));
    }

    void catalogsMatchTheEnglishOne() {
        // A catalog may lack a string (it shows in English meanwhile), but what
        // it has translates something in en.json, with the same placeholders,
        // and a counted string has every form its language uses.
        const QString folder = QFileInfo(QFINDTESTDATA("../src/i18n/en.json")).absolutePath();
        const QJsonObject english = readCatalog(folder + "/en.json");
        QStringList problems;
        for (const QFileInfo &file : QDir(folder).entryInfoList({"*.json"}, QDir::Files)) {
            const QString code = file.completeBaseName();
            const QJsonObject catalog = readCatalog(file.filePath());
            if (catalog.isEmpty())
                problems << code + ": not a JSON object, or empty";
            // Some languages reach a form only at a million ("many" in French).
            QSet<QString> categories{"other", Translator::pluralCategory(code, 1000000)};
            for (long long n = 0; n <= 1000; ++n)
                categories << Translator::pluralCategory(code, n);
            for (auto it = catalog.begin(); it != catalog.end(); ++it) {
                const QString where = code + ": " + it.key() + ": ";
                const QJsonValue source = english.value(it.key());
                if (source.isUndefined()) {
                    problems << where + "not in en.json";
                } else if (source.isString()) {
                    if (!it.value().isString() || it.value().toString().isEmpty())
                        problems << where + "needs a translation";
                    else if (placeholders(it.value().toString()) != placeholders(it.key()))
                        problems << where + "has other placeholders than the English";
                } else {
                    const QJsonObject forms = it.value().toObject();
                    const QStringList wanted = placeholders(source.toObject().value("other").toString());
                    for (const QString &category : categories)
                        if (forms.value(category).toString().isEmpty())
                            problems << where + "needs its " + category + " form";
                    // A form other than "other" may say its number in words.
                    for (auto form = forms.begin(); form != forms.end(); ++form) {
                        const QStringList has = placeholders(form.value().toString());
                        QStringList needed = wanted;
                        if (form.key() != "other")
                            needed.removeIf([&](const QString &p) { return p.endsWith('n') && !has.contains(p); });
                        if (has != needed)
                            problems << where + form.key() + " has other placeholders than the English";
                    }
                }
            }
        }
        QVERIFY2(problems.isEmpty(), qPrintable(problems.join('\n')));
    }

    void windowLoadsAndAnswersTheKeyboard() {
        // A temp language and theme, so the test never depends on the desktop's.
        QTemporaryDir dir;
        Translator translator(dir.path() + "/language");
        QCoreApplication::installTranslator(&translator);
        Theme theme(dir.path());
        QVERIFY(write(dir.path() + "/Glass_Rivers.pdf", "%PDF"));
        FakePicker picker;
        Backend backend(dir.path() + "/shelf", &picker);
        backend.setShareFolder(dir.path() + "/shared");

        QQmlApplicationEngine engine;
        QStringList warnings;
        connect(&engine, &QQmlEngine::warnings, this, [&](const QList<QQmlError> &errors) {
            for (const auto &error : errors)
                warnings << error.toString();
        });
        engine.rootContext()->setContextProperty("translator", &translator);
        engine.rootContext()->setContextProperty("theme", &theme);
        engine.rootContext()->setContextProperty("backend", &backend);
        engine.load(QUrl("qrc:/Main.qml"));
        QVERIFY(!engine.rootObjects().isEmpty());
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        QVERIFY(window);
        QVERIFY(QTest::qWaitForWindowExposed(window));
        window->requestActivate();
        QVERIFY(QTest::qWaitForWindowActive(window));

        auto *count = item(window->contentItem(), "bookCount");
        auto *empty = item(window->contentItem(), "empty");
        auto *shelf = item(window->contentItem(), "shelf");
        QVERIFY(count && empty && shelf);
        QCOMPARE(count->property("text").toString(), QString("0 books"));
        QVERIFY(empty->property("visible").toBool());

        // N opens the new book dialog with the title ready to type into, and
        // Enter puts the book on the shelf.
        auto *newBook = window->findChild<QObject *>("newBook");
        QTest::keyClick(window, Qt::Key_N);
        QTRY_VERIFY(newBook->property("opened").toBool());
        auto *title = window->findChild<QQuickItem *>("titleField");
        QTRY_VERIFY(title->hasActiveFocus());
        // Letters typed into the title are text, not the shelf's shortcuts.
        for (const QChar letter : QString("Qn"))
            QTest::keyClick(window, letter.toLatin1());
        QCOMPARE(title->property("text").toString(), QString("Qn"));
        QTest::keyClick(window, Qt::Key_Return);
        QTRY_VERIFY(!newBook->property("visible").toBool());

        QCOMPARE(backend.books().size(), 1);
        QCOMPARE(backend.books().at(0).toMap().value("title").toString(), QString("Qn"));
        QCOMPARE(count->property("text").toString(), QString("1 book"));
        QVERIFY(!empty->property("visible").toBool());
        QCOMPARE(shelf->property("count").toInt(), 1);
        QCOMPARE(shelf->property("currentIndex").toInt(), 0);

        // The caption reads on the accent: black on the fallback yellow.
        QTest::keyClick(window, Qt::Key_N);
        QTRY_VERIFY(newBook->property("opened").toBool());
        auto *button = window->findChild<QQuickItem *>("createButton");
        QVERIFY(button);
        QCOMPARE(button->property("contentItem").value<QQuickItem *>()->property("color").value<QColor>(),
                 theme.accentForeground());
        QTest::keyClick(window, Qt::Key_Escape);
        QTRY_VERIFY(!newBook->property("visible").toBool());
        QCOMPARE(backend.books().size(), 1);

        // Enter opens the picked book as a page; what is typed there is the
        // book's text, the shelf's own keys included.
        auto *page = window->findChild<QQuickItem *>("page");
        auto *editor = window->findChild<QQuickItem *>("editorView");
        QVERIFY(page && editor);
        QTest::keyClick(window, Qt::Key_Return);
        QTRY_VERIFY(editor->property("visible").toBool());
        QTRY_VERIFY(page->hasActiveFocus());
        for (const QChar letter : QString("nq"))
            QTest::keyClick(window, letter.toLatin1());
        QTest::keyClick(window, Qt::Key_Return);
        QTest::keyClick(window, '?');
        QCOMPARE(page->property("text").toString(), QString("nq\n?"));
        QVERIFY(!newBook->property("opened").toBool());
        QVERIFY(page->property("unsaved").toBool());

        // Escape saves the page and goes back to the shelf.
        const QString bookId = backend.books().at(0).toMap().value("id").toString();
        QTest::keyClick(window, Qt::Key_Escape);
        QTRY_VERIFY(!editor->property("visible").toBool());
        QCOMPARE(backend.readText(bookId), QString("nq\n?"));
        QVERIFY(shelf->hasActiveFocus());
        // Leaving happens in the same keypress as the save, so the shelf is
        // where the writer is told the book was saved.
        auto *notice = item(window->contentItem(), "notice");
        QVERIFY(notice);
        QVERIFY(notice->property("visible").toBool());
        QCOMPARE(notice->property("text").toString(), QString("Saved"));
        // Opening and leaving without writing has nothing to announce.
        window->setProperty("notice", QString());
        QTest::keyClick(window, Qt::Key_Return);
        QTRY_VERIFY(editor->property("visible").toBool());
        QTest::keyClick(window, Qt::Key_Escape);
        QTRY_VERIFY(!editor->property("visible").toBool());
        QVERIFY(!notice->property("visible").toBool());

        // In a book, Ctrl B and Ctrl I put stars around the picked words,
        // and take them away again.
        QTest::keyClick(window, Qt::Key_Return);
        QTRY_VERIFY(editor->property("visible").toBool());
        QTRY_VERIFY(page->hasActiveFocus());
        QMetaObject::invokeMethod(page, "select", Q_ARG(int, 0), Q_ARG(int, 2));
        QTest::keyClick(window, Qt::Key_B, Qt::ControlModifier);
        QCOMPARE(page->property("text").toString(), QString("**nq**\n?"));
        QCOMPARE(page->property("selectedText").toString(), QString("nq"));
        QTest::keyClick(window, Qt::Key_I, Qt::ControlModifier);
        QCOMPARE(page->property("text").toString(), QString("***nq***\n?"));
        QTest::keyClick(window, Qt::Key_B, Qt::ControlModifier);
        QCOMPARE(page->property("text").toString(), QString("*nq*\n?"));
        QTest::keyClick(window, Qt::Key_I, Qt::ControlModifier);
        QCOMPARE(page->property("text").toString(), QString("nq\n?"));
        // With nothing picked the stars arrive with the cursor between them.
        QMetaObject::invokeMethod(page, "select", Q_ARG(int, 2), Q_ARG(int, 2));
        QTest::keyClick(window, Qt::Key_I, Qt::ControlModifier);
        QTest::keyClick(window, Qt::Key_X);
        QCOMPARE(page->property("text").toString(), QString("nq*x*\n?"));
        QTest::keyClick(window, Qt::Key_Escape);
        QTRY_VERIFY(!editor->property("visible").toBool());
        QCOMPARE(backend.readText(bookId), QString("nq*x*\n?"));
        window->setProperty("notice", QString());

        // C asks the desktop for a picture and stands it on the picked book.
        QVERIFY(paint(dir.path() + "/roses.png", Qt::red));
        picker.pickedCover = QUrl::fromLocalFile(dir.path() + "/roses.png");
        QTest::keyClick(window, Qt::Key_C);
        QCOMPARE(picker.askedForCover, 1);
        QVERIFY(!backend.books().at(0).toMap().value("coverUrl").toString().isEmpty());
        QCOMPARE(shelf->property("currentIndex").toInt(), 0);

        // A asks the desktop for files; the picked one lands on the shelf
        // and is the book picked there.
        picker.picked = {QUrl::fromLocalFile(dir.path() + "/Glass_Rivers.pdf")};
        QTest::keyClick(window, Qt::Key_A);
        QCOMPARE(picker.asked, 1);
        QTRY_COMPARE(shelf->property("count").toInt(), 2);
        QTRY_COMPARE(shelf->property("currentIndex").toInt(), 1);
        QCOMPARE(count->property("text").toString(), QString("2 books"));

        // S hands the picked book over as a file and says where it went.
        QTest::keyClick(window, Qt::Key_S);
        QTRY_VERIFY(notice->property("visible").toBool());
        QVERIFY(notice->property("text").toString().endsWith("/shared/Glass Rivers.pdf"));
        QVERIFY(QFile::exists(dir.path() + "/shared/Glass Rivers.pdf"));

        auto *help = window->findChild<QObject *>("help");
        QTest::keyClick(window, '?');
        QTRY_VERIFY(help->property("opened").toBool());
        // A Material popup animates out: `opened` drops at once, `visible` after.
        QTest::keyClick(window, Qt::Key_Escape);
        QTRY_VERIFY(!help->property("visible").toBool());

        QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join('\n')));
        QCoreApplication::removeTranslator(&translator);
    }
};

QTEST_MAIN(HaskyShelfTests)
#include "haskyshelf_tests.moc"
