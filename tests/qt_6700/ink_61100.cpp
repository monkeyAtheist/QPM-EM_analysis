#include "widgets/notes_ink_canvas.h"

#include <QApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMouseEvent>
#include <QTest>

#include <cmath>

class Ink61100Test final : public QObject
{
    Q_OBJECT
private:
    static void drag(NotesInkCanvas &canvas, QPoint from, QPoint to)
    {
        QTest::mousePress(&canvas, Qt::LeftButton, Qt::NoModifier, from);
        QMouseEvent moving(QEvent::MouseMove, QPointF(to), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(&canvas, &moving);
        QTest::mouseRelease(&canvas, Qt::LeftButton, Qt::NoModifier, to);
    }
    static QJsonObject currentPage(const NotesInkCanvas &canvas)
    {
        const QJsonObject root=QJsonDocument::fromJson(canvas.serialize()).object();
        return root.value(QStringLiteral("pages")).toArray().at(canvas.currentPageIndex()).toObject();
    }
private slots:
    void zoomAndPanAreViewOnly()
    {
        NotesInkCanvas canvas;
        canvas.resize(1600,1000);
        canvas.show();
        QTest::qWait(20);
        const QPointF paper(800,500);
        const QPointF before=canvas.pageToWidget(paper);
        QCOMPARE(before,QPointF(800,500));
        canvas.addTextAt({200,200},QStringLiteral("unité SI"));
        const QByteArray original=canvas.serialize();
        canvas.setZoomFactor(2.0,canvas.pageToWidget(paper));
        QVERIFY(std::abs(canvas.zoomFactor()-2.0)<1e-9);
        QVERIFY(std::hypot((canvas.widgetToPage(canvas.pageToWidget({210,400}))-QPointF(210,400)).x(),
                           (canvas.widgetToPage(canvas.pageToWidget({210,400}))-QPointF(210,400)).y())<1e-6);
        canvas.setTool(NotesInkCanvas::Tool::Hand);
        drag(canvas,{400,400},{490,460});
        QVERIFY(canvas.pageToWidget(paper)!=before);
        QCOMPARE(canvas.serialize(),original); // no undo or autosave on viewport changes
        canvas.fitPage();
        QCOMPARE(canvas.zoomFactor(),1.0);
        QCOMPARE(canvas.pageToWidget(paper),before);
    }

    void embeddedPngUndoMoveAndReload()
    {
        NotesInkCanvas canvas;
        canvas.resize(1600,1000);
        canvas.show();
        QTest::qWait(20);
        QImage image(120,80,QImage::Format_ARGB32);
        image.fill(QColor(40,145,230));
        QVERIFY(canvas.addImage(image,QPointF(300,250)));
        QCOMPARE(canvas.imageCount(),1);
        QVERIFY(canvas.renderToImage().pixelColor(310,260)==QColor(40,145,230));
        const QJsonObject json=QJsonDocument::fromJson(canvas.serialize()).object();
        QCOMPARE(json.value(QStringLiteral("version")).toInt(),6);
        QCOMPARE(currentPage(canvas).value(QStringLiteral("images")).toArray().size(),1);
        canvas.undo(); QCOMPARE(canvas.imageCount(),0);
        canvas.redo(); QCOMPARE(canvas.imageCount(),1);
        canvas.setTool(NotesInkCanvas::Tool::Select);
        drag(canvas,{315,265},{400,300});
        const QJsonObject moved=currentPage(canvas).value(QStringLiteral("images")).toArray().at(0).toObject();
        QCOMPARE(moved.value(QStringLiteral("x")).toInt(),385);
        QCOMPARE(moved.value(QStringLiteral("y")).toInt(),285);
        canvas.undo();
        const QJsonObject restored=currentPage(canvas).value(QStringLiteral("images")).toArray().at(0).toObject();
        QCOMPARE(restored.value(QStringLiteral("x")).toInt(),300);
        const QByteArray saved=canvas.serialize();
        NotesInkCanvas loaded;
        QString error;
        QVERIFY2(loaded.loadDocument(saved,&error),qPrintable(error));
        QCOMPARE(loaded.imageCount(),1);
        QCOMPARE(loaded.renderToImage().pixelColor(310,260),QColor(40,145,230));
        loaded.clearDocument(); QCOMPARE(loaded.imageCount(),0);
        loaded.undo(); QCOMPARE(loaded.imageCount(),1);
    }

    void legacyV3AndInvalidImageAreTransactional()
    {
        NotesInkCanvas canvas;
        canvas.addTextAt({100,200},QStringLiteral("Conserver"));
        QJsonObject legacy=QJsonDocument::fromJson(canvas.serialize()).object();
        legacy.insert(QStringLiteral("version"),3);
        QJsonArray entries=legacy.value(QStringLiteral("pages")).toArray();
        QJsonObject first=entries.at(0).toObject();
        first.remove(QStringLiteral("images"));
        entries.replace(0,first);
        legacy.insert(QStringLiteral("pages"),entries);
        QString error;
        QVERIFY2(canvas.loadDocument(QJsonDocument(legacy).toJson(),&error),qPrintable(error));
        QCOMPARE(canvas.textCount(),1);
        QCOMPARE(canvas.imageCount(),0);
        const QByteArray before=canvas.serialize();
        QJsonObject corrupt=QJsonDocument::fromJson(before).object();
        entries=corrupt.value(QStringLiteral("pages")).toArray();
        first=entries.at(0).toObject();
        QJsonObject image;
        image.insert(QStringLiteral("x"),300);
        image.insert(QStringLiteral("y"),200);
        image.insert(QStringLiteral("w"),200);
        image.insert(QStringLiteral("h"),150);
        image.insert(QStringLiteral("png"),QStringLiteral("not-base64-!!!"));
        first.insert(QStringLiteral("images"),QJsonArray{image});
        entries.replace(0,first);
        corrupt.insert(QStringLiteral("pages"),entries);
        QVERIFY(!canvas.loadDocument(QJsonDocument(corrupt).toJson(),&error));
        QCOMPARE(canvas.serialize(),before);
    }
};

QTEST_MAIN(Ink61100Test)
#include "ink_61100.moc"
