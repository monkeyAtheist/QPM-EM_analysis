#include "widgets/notes_ink_canvas.h"

#include <QApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>

class Ink6900Test final : public QObject
{
    Q_OBJECT
private slots:
    void legacyMigrationAndPagePersistence()
    {
        // Reproduce a valid 6.8.0 file; it must import as exactly one page.
        QJsonObject legacy;
        legacy.insert(QStringLiteral("version"), 1);
        legacy.insert(QStringLiteral("width"), 1600);
        legacy.insert(QStringLiteral("height"), 1000);
        QJsonObject stroke;
        stroke.insert(QStringLiteral("color"), QStringLiteral("#1b3656"));
        stroke.insert(QStringLiteral("width"), 3.5);
        stroke.insert(QStringLiteral("points"), QJsonArray{QJsonArray{800, 500, 1.0}});
        legacy.insert(QStringLiteral("strokes"), QJsonArray{stroke});
        NotesInkCanvas notes;
        QString error;
        QVERIFY(notes.loadDocument(QJsonDocument(legacy).toJson(), &error));
        QCOMPARE(notes.pageCount(), 1);
        QCOMPARE(notes.strokeCount(), 1);
        QCOMPARE(notes.textCount(), 0);

        notes.addTextAt(QPointF(300, 150), QStringLiteral("E = ke*q/r²\\nValeur mesurée"));
        QCOMPARE(notes.textCount(), 1);
        notes.undo();
        QCOMPARE(notes.textCount(), 0);
        notes.redo();
        QCOMPARE(notes.textCount(), 1);
        QVERIFY(notes.addPage());
        QCOMPARE(notes.pageCount(), 2);
        QCOMPARE(notes.currentPageIndex(), 1);
        notes.addTextAt(QPointF(40, 40), QStringLiteral("Page 2 — notes de laboratoire"));
        QCOMPARE(notes.strokeCount(), 0);
        QCOMPARE(notes.textCount(), 1);
        QVERIFY(notes.setCurrentPage(0));
        QCOMPARE(notes.strokeCount(), 1);
        QCOMPARE(notes.textCount(), 1);
        QVERIFY(notes.setCurrentPage(1));
        const QByteArray roundtrip = notes.serialize();
        const QJsonObject parsed = QJsonDocument::fromJson(roundtrip).object();
        QCOMPARE(parsed.value(QStringLiteral("version")).toInt(), 6);
        QCOMPARE(parsed.value(QStringLiteral("pages")).toArray().size(), 2);
        NotesInkCanvas loaded;
        QVERIFY(loaded.loadDocument(roundtrip, &error));
        QCOMPARE(loaded.currentPageIndex(), 1);
        QCOMPARE(loaded.textCount(), 1);
        QVERIFY(loaded.setCurrentPage(0));
        QCOMPARE(loaded.strokeCount(), 1);
        QCOMPARE(loaded.textCount(), 1);
        QVERIFY(loaded.removeCurrentPage());
        QCOMPARE(loaded.pageCount(), 1);
        QVERIFY(!loaded.removeCurrentPage());
    }

    void invalidPageMustNotEraseValidNotebook()
    {
        NotesInkCanvas notes;
        notes.addTextAt(QPointF(150, 150), QStringLiteral("Conserver ce texte"));
        QVERIFY(notes.addPage());
        const QByteArray before = notes.serialize();
        QJsonObject corrupted = QJsonDocument::fromJson(before).object();
        QJsonArray pages = corrupted.value(QStringLiteral("pages")).toArray();
        QJsonObject p = pages.at(1).toObject();
        QJsonArray texts = p.value(QStringLiteral("texts")).toArray();
        QJsonObject invalid;
        invalid.insert(QStringLiteral("x"), 1e100);
        invalid.insert(QStringLiteral("y"), 20);
        invalid.insert(QStringLiteral("size"), 32);
        invalid.insert(QStringLiteral("text"), QStringLiteral("bad"));
        invalid.insert(QStringLiteral("color"), QStringLiteral("#000000"));
        texts.append(invalid);
        p.insert(QStringLiteral("texts"), texts);
        pages.replace(1, p);
        corrupted.insert(QStringLiteral("pages"), pages);
        QString error;
        QVERIFY(!notes.loadDocument(QJsonDocument(corrupted).toJson(), &error));
        QVERIFY(!error.isEmpty());
        QCOMPARE(notes.serialize(), before);
    }

    void perPageUndoAndMouse()
    {
        NotesInkCanvas notes;
        notes.resize(1100, 750);
        notes.show();
        QTest::qWait(20);
        const QPoint center(notes.width()/2, notes.height()/2);
        QTest::mouseClick(&notes, Qt::LeftButton, Qt::NoModifier, center);
        QCOMPARE(notes.strokeCount(), 1);
        QVERIFY(notes.addPage());
        QTest::mouseClick(&notes, Qt::LeftButton, Qt::NoModifier, center);
        QCOMPARE(notes.strokeCount(), 1);
        notes.undo();
        QCOMPARE(notes.strokeCount(), 0);
        QVERIFY(notes.setCurrentPage(0));
        QCOMPARE(notes.strokeCount(), 1);
        QVERIFY(notes.canUndo());
    }
};
QTEST_MAIN(Ink6900Test)
#include "ink_6900.moc"
