#include "widgets/notes_ink_canvas.h"

#include <QApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMouseEvent>
#include <QTest>

class Ink61000Test final : public QObject
{
    Q_OBJECT
private:
    static QJsonObject currentPage(const NotesInkCanvas &canvas)
    {
        const QJsonObject root = QJsonDocument::fromJson(canvas.serialize()).object();
        return root.value(QStringLiteral("pages")).toArray().at(canvas.currentPageIndex()).toObject();
    }
    static void drag(NotesInkCanvas &canvas, const QPoint &from, const QPoint &to,
                     Qt::KeyboardModifiers mods = Qt::NoModifier)
    {
        QTest::mousePress(&canvas, Qt::LeftButton, mods, from);
        QMouseEvent move(QEvent::MouseMove, QPointF(to), Qt::NoButton, Qt::LeftButton, mods);
        QApplication::sendEvent(&canvas, &move);
        QTest::mouseRelease(&canvas, Qt::LeftButton, mods, to);
    }
private slots:
    void shapesUndoRedoAndMultipageRoundtrip()
    {
        NotesInkCanvas canvas;
        canvas.addShape(NotesInkCanvas::Tool::Line, {100, 100}, {800, 100});
        canvas.addShape(NotesInkCanvas::Tool::Arrow, {100, 200}, {800, 400});
        canvas.addShape(NotesInkCanvas::Tool::Rectangle, {300, 250}, {700, 450});
        canvas.addShape(NotesInkCanvas::Tool::Ellipse, {500, 600}, {900, 900});
        QCOMPARE(canvas.shapeCount(), 4);
        canvas.undo(); QCOMPARE(canvas.shapeCount(), 3);
        canvas.redo(); QCOMPARE(canvas.shapeCount(), 4);
        const QJsonArray shapes = currentPage(canvas).value(QStringLiteral("shapes")).toArray();
        QCOMPARE(shapes.at(0).toObject().value(QStringLiteral("type")).toString(), QStringLiteral("line"));
        QCOMPARE(shapes.at(1).toObject().value(QStringLiteral("type")).toString(), QStringLiteral("arrow"));
        QCOMPARE(shapes.at(2).toObject().value(QStringLiteral("type")).toString(), QStringLiteral("rectangle"));
        QCOMPARE(shapes.at(3).toObject().value(QStringLiteral("type")).toString(), QStringLiteral("ellipse"));
        QVERIFY(!canvas.renderToImage().isNull());
        QVERIFY(canvas.addPage());
        canvas.addShape(NotesInkCanvas::Tool::Arrow, {50, 60}, {220, 230});
        QCOMPARE(canvas.shapeCount(), 1);
        const QByteArray saved = canvas.serialize();
        QCOMPARE(QJsonDocument::fromJson(saved).object().value(QStringLiteral("version")).toInt(), 6);
        NotesInkCanvas restored;
        QString error;
        QVERIFY2(restored.loadDocument(saved, &error), qPrintable(error));
        QCOMPARE(restored.pageCount(), 2);
        QCOMPARE(restored.shapeCount(), 1);
        QVERIFY(restored.setCurrentPage(0));
        QCOMPARE(restored.shapeCount(), 4);
        restored.clearDocument(); QCOMPARE(restored.shapeCount(), 0);
        restored.undo(); QCOMPARE(restored.shapeCount(), 4);
    }

    void selectionMovesTextShapeAndStrokeUndoably()
    {
        NotesInkCanvas canvas;
        canvas.resize(1600, 1000);
        canvas.show();
        QTest::qWait(30);
        canvas.addTextAt({500, 100}, QStringLiteral("Champ E"));
        canvas.addShape(NotesInkCanvas::Tool::Line, {200, 300}, {430, 300});
        canvas.setTool(NotesInkCanvas::Tool::Pen);
        QTest::mouseClick(&canvas, Qt::LeftButton, Qt::NoModifier, QPoint(800, 600));
        QCOMPARE(canvas.strokeCount(), 1);
        canvas.setTool(NotesInkCanvas::Tool::Select);
        drag(canvas, {250,300}, {290,335});
        auto shape = currentPage(canvas).value(QStringLiteral("shapes")).toArray().at(0).toObject();
        QCOMPARE(shape.value(QStringLiteral("x1")).toInt(), 240);
        QCOMPARE(shape.value(QStringLiteral("y1")).toInt(), 335);
        canvas.undo();
        shape = currentPage(canvas).value(QStringLiteral("shapes")).toArray().at(0).toObject();
        QCOMPARE(shape.value(QStringLiteral("x1")).toInt(), 200);
        canvas.redo();
        shape = currentPage(canvas).value(QStringLiteral("shapes")).toArray().at(0).toObject();
        QCOMPARE(shape.value(QStringLiteral("x1")).toInt(), 240);
        drag(canvas, {507, 107}, {570, 150});
        auto text = currentPage(canvas).value(QStringLiteral("texts")).toArray().at(0).toObject();
        QCOMPARE(text.value(QStringLiteral("x")).toInt(), 563);
        QCOMPARE(text.value(QStringLiteral("y")).toInt(), 143);
        drag(canvas, {800,600}, {810,640});
        auto samples = currentPage(canvas).value(QStringLiteral("strokes")).toArray().at(0)
            .toObject().value(QStringLiteral("points")).toArray();
        QCOMPARE(samples.at(0).toArray().at(0).toInt(), 810);
        QCOMPARE(samples.at(0).toArray().at(1).toInt(), 640);
        canvas.undo();
        samples = currentPage(canvas).value(QStringLiteral("strokes")).toArray().at(0)
            .toObject().value(QStringLiteral("points")).toArray();
        QCOMPARE(samples.at(0).toArray().at(0).toInt(), 800);
        canvas.setTool(NotesInkCanvas::Tool::Eraser);
        QTest::mouseClick(&canvas, Qt::LeftButton, Qt::NoModifier, QPoint(260, 335));
        QCOMPARE(canvas.shapeCount(), 0);
        canvas.undo(); QCOMPARE(canvas.shapeCount(), 1);
    }

    void v2MigrationAndInvalidShapeAreTransactional()
    {
        NotesInkCanvas canvas;
        canvas.addTextAt({120, 150}, QStringLiteral("Keep me"));
        const QJsonObject v3 = QJsonDocument::fromJson(canvas.serialize()).object();
        QJsonObject v2 = v3;
        v2.insert(QStringLiteral("version"), 2);
        QJsonArray pages = v2.value(QStringLiteral("pages")).toArray();
        QJsonObject page = pages.at(0).toObject();
        page.remove(QStringLiteral("shapes"));
        pages.replace(0, page);
        v2.insert(QStringLiteral("pages"), pages);
        QString error;
        QVERIFY2(canvas.loadDocument(QJsonDocument(v2).toJson(), &error), qPrintable(error));
        QCOMPARE(canvas.textCount(), 1);
        QCOMPARE(canvas.shapeCount(), 0);
        const QByteArray before = canvas.serialize();
        QJsonObject invalid = QJsonDocument::fromJson(before).object();
        pages = invalid.value(QStringLiteral("pages")).toArray();
        page = pages.at(0).toObject();
        QJsonObject bad;
        bad.insert(QStringLiteral("type"), QStringLiteral("line"));
        bad.insert(QStringLiteral("x1"), 1e100);
        bad.insert(QStringLiteral("y1"), 2);
        bad.insert(QStringLiteral("x2"), 30);
        bad.insert(QStringLiteral("y2"), 40);
        bad.insert(QStringLiteral("width"), 3);
        bad.insert(QStringLiteral("color"), QStringLiteral("#123456"));
        page.insert(QStringLiteral("shapes"), QJsonArray{bad});
        pages.replace(0, page);
        invalid.insert(QStringLiteral("pages"), pages);
        QVERIFY(!canvas.loadDocument(QJsonDocument(invalid).toJson(), &error));
        QVERIFY(!error.isEmpty());
        QCOMPARE(canvas.serialize(), before);
    }

    void shiftConstrainsEllipseToCircle()
    {
        NotesInkCanvas canvas;
        canvas.resize(1600, 1000);
        canvas.show();
        QTest::qWait(30);
        canvas.setTool(NotesInkCanvas::Tool::Ellipse);
        drag(canvas, {200, 200}, {370, 280}, Qt::ShiftModifier);
        QCOMPARE(canvas.shapeCount(), 1);
        const QJsonObject shape = currentPage(canvas).value(QStringLiteral("shapes")).toArray().at(0).toObject();
        const double dx = shape.value(QStringLiteral("x2")).toDouble() - shape.value(QStringLiteral("x1")).toDouble();
        const double dy = shape.value(QStringLiteral("y2")).toDouble() - shape.value(QStringLiteral("y1")).toDouble();
        QCOMPARE(dx, dy);
    }
};
QTEST_MAIN(Ink61000Test)
#include "ink_61000.moc"
