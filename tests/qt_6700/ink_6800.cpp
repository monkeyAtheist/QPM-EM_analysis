#include "widgets/notes_ink_canvas.h"
#include <QApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>

class Ink6800Test : public QObject
{
    Q_OBJECT
private slots:
    void penAndMouseRoundTrip()
    {
        NotesInkCanvas canvas;
        canvas.resize(1000, 660);
        canvas.show();
        QTest::qWait(20);
        const QPoint center(canvas.width() / 2, canvas.height() / 2);
        QTest::mouseClick(&canvas, Qt::LeftButton, Qt::NoModifier, center);
        QCOMPARE(canvas.strokeCount(), 1);
        QVERIFY(canvas.canUndo());
        const QByteArray json = canvas.serialize();
        QCOMPARE(QJsonDocument::fromJson(json).object().value(QStringLiteral("version")).toInt(), 6);
        NotesInkCanvas loaded;
        QString error;
        QVERIFY(loaded.loadDocument(json, &error));
        QCOMPARE(loaded.strokeCount(), 1);
        QVERIFY(!loaded.loadDocument(QByteArrayLiteral("{invalid"), &error));
        QCOMPARE(loaded.strokeCount(), 1); // malformed file must not erase notes
        const QImage png = loaded.renderToImage();
        QCOMPARE(png.width(), 1600);
        QCOMPARE(png.height(), 1000);
        canvas.undo();
        QCOMPARE(canvas.strokeCount(), 0);
        canvas.redo();
        QCOMPARE(canvas.strokeCount(), 1);
        canvas.setTool(NotesInkCanvas::Tool::Eraser);
        QTest::mouseClick(&canvas, Qt::LeftButton, Qt::NoModifier, center);
        QCOMPARE(canvas.strokeCount(), 0);
        canvas.undo();
        QCOMPARE(canvas.strokeCount(), 1);
        canvas.clearDocument();
        QCOMPARE(canvas.strokeCount(), 0);
        canvas.undo();
        QCOMPARE(canvas.strokeCount(), 1);
    }
};
QTEST_MAIN(Ink6800Test)
#include "ink_6800.moc"
