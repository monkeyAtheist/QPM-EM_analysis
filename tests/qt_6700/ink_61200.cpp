#include "widgets/notes_ink_canvas.h"

#include <QApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMouseEvent>
#include <QTest>

#include <cmath>

class Ink61200Test final : public QObject
{
    Q_OBJECT
private:
    static QJsonArray symbols(const NotesInkCanvas &canvas)
    {
        const auto json=QJsonDocument::fromJson(canvas.serialize()).object();
        return json.value(QStringLiteral("pages")).toArray()
            .at(canvas.currentPageIndex()).toObject().value(QStringLiteral("symbols")).toArray();
    }
    static void drag(NotesInkCanvas &canvas, QPoint start, QPoint end)
    {
        QTest::mousePress(&canvas,Qt::LeftButton,Qt::NoModifier,start);
        QMouseEvent moving(QEvent::MouseMove,QPointF(end),Qt::NoButton,Qt::LeftButton,Qt::NoModifier);
        QApplication::sendEvent(&canvas,&moving);
        QTest::mouseRelease(&canvas,Qt::LeftButton,Qt::NoModifier,end);
    }
private slots:
    void placeMoveRotateResizeUndoRedo()
    {
        NotesInkCanvas canvas;
        canvas.resize(1600,1000);
        canvas.show();
        QTest::qWait(30);
        QCOMPARE(canvas.symbolCount(),0);
        canvas.setTool(NotesInkCanvas::Tool::Component);
        canvas.setSymbolType(QStringLiteral("bjt_npn"));
        QTest::mouseClick(&canvas,Qt::LeftButton,Qt::NoModifier,{400,360});
        QCOMPARE(canvas.symbolCount(),1);
        QCOMPARE(symbols(canvas).at(0).toObject().value(QStringLiteral("id")).toString(),QStringLiteral("bjt_npn"));
        QCOMPARE(symbols(canvas).at(0).toObject().value(QStringLiteral("reference")).toString(),QStringLiteral("Q1"));
        canvas.setTool(NotesInkCanvas::Tool::Select);
        drag(canvas,{400,360},{480,400});
        QCOMPARE(symbols(canvas).at(0).toObject().value(QStringLiteral("x")).toDouble(),480.0);
        QCOMPARE(symbols(canvas).at(0).toObject().value(QStringLiteral("y")).toDouble(),400.0);
        QVERIFY(canvas.hasSelectedSymbol());
        QVERIFY(canvas.rotateSelectedSymbol(90));
        QVERIFY(canvas.resizeSelectedSymbol(1.5));
        QVERIFY(canvas.renameSelectedSymbol(QStringLiteral("Q_DRIVER")));
        QCOMPARE(symbols(canvas).at(0).toObject().value(QStringLiteral("rotation")).toInt(),90);
        QCOMPARE(symbols(canvas).at(0).toObject().value(QStringLiteral("scale")).toDouble(),1.5);
        QCOMPARE(symbols(canvas).at(0).toObject().value(QStringLiteral("reference")).toString(),QStringLiteral("Q_DRIVER"));
        canvas.undo(); // rename
        QCOMPARE(symbols(canvas).at(0).toObject().value(QStringLiteral("reference")).toString(),QStringLiteral("Q1"));
        canvas.undo(); // resize
        QCOMPARE(symbols(canvas).at(0).toObject().value(QStringLiteral("scale")).toDouble(),1.0);
        canvas.undo(); // rotation
        QCOMPARE(symbols(canvas).at(0).toObject().value(QStringLiteral("rotation")).toInt(),0);
        canvas.undo(); // move
        QCOMPARE(symbols(canvas).at(0).toObject().value(QStringLiteral("x")).toDouble(),400.0);
        canvas.redo();canvas.redo();canvas.redo();canvas.redo();
        QCOMPARE(symbols(canvas).at(0).toObject().value(QStringLiteral("reference")).toString(),QStringLiteral("Q_DRIVER"));
        const auto saved=canvas.serialize();
        NotesInkCanvas restored;
        QString error;
        QVERIFY2(restored.loadDocument(saved,&error),qPrintable(error));
        QCOMPARE(restored.symbolCount(),1);
        QCOMPARE(restored.renderToImage().size(),QSize(1600,1000));
        QVERIFY(canvas.deleteSelectedSymbol());
        QCOMPARE(canvas.symbolCount(),0);
        canvas.undo();
        QCOMPARE(canvas.symbolCount(),1);
    }
    void imageCornerResizeIsUndoable()
    {
        NotesInkCanvas canvas;
        canvas.resize(1600,1000);canvas.show();QTest::qWait(30);
        QImage image(120,80,QImage::Format_ARGB32);image.fill(Qt::red);
        QVERIFY(canvas.addImage(image,{300,250}));
        canvas.setTool(NotesInkCanvas::Tool::Select);
        QTest::mouseClick(&canvas,Qt::LeftButton,Qt::NoModifier,{320,270});
        const auto images=[&canvas]() {
            const auto root=QJsonDocument::fromJson(canvas.serialize()).object();
            return root.value(QStringLiteral("pages")).toArray().at(0).toObject()
                .value(QStringLiteral("images")).toArray();
        };
        QCOMPARE(images().at(0).toObject().value(QStringLiteral("w")).toDouble(),120.0);
        drag(canvas,{420,330},{520,395});
        QVERIFY(images().at(0).toObject().value(QStringLiteral("w")).toDouble()>120.0);
        canvas.undo();
        QCOMPARE(images().at(0).toObject().value(QStringLiteral("w")).toDouble(),120.0);
        canvas.redo();
        QVERIFY(images().at(0).toObject().value(QStringLiteral("w")).toDouble()>120.0);
    }
    void catalogAndMigratedV4()
    {
        const QStringList ids={"resistor","resistor_us","potentiometer","capacitor",
            "capacitor_polar","inductor","transformer","fuse","bjt_npn","bjt_pnp",
            "mos_n","mos_p","jfet_n","jfet_p","opamp","diode","zener","led",
            "voltage_dc","voltage_ac","current_source","battery","ground","switch",
            "junction","crystal","antenna"};
        NotesInkCanvas canvas;
        for (int i=0;i<ids.size();++i)
            QVERIFY2(canvas.addSymbol(ids[i],QPointF(150+(i%9)*145,150+(i/9)*265),0.7,0),
                qPrintable(ids[i]));
        QCOMPARE(canvas.symbolCount(),27);
        QVERIFY(!canvas.renderToImage().isNull());
        QCOMPARE(QJsonDocument::fromJson(canvas.serialize()).object().value(QStringLiteral("version")).toInt(),6);
        NotesInkCanvas saved;
        QString error;
        QVERIFY2(saved.loadDocument(canvas.serialize(),&error),qPrintable(error));
        QCOMPARE(saved.symbolCount(),27);
        QJsonObject v4=QJsonDocument::fromJson(canvas.serialize()).object();
        QJsonArray pages=v4.value(QStringLiteral("pages")).toArray();
        auto page=pages.at(0).toObject();
        page.remove(QStringLiteral("symbols"));
        pages.replace(0,page);
        v4.insert(QStringLiteral("pages"),pages);
        v4.insert(QStringLiteral("version"),4);
        QVERIFY2(saved.loadDocument(QJsonDocument(v4).toJson(),&error),qPrintable(error));
        QCOMPARE(saved.symbolCount(),0);
        const QByteArray before=saved.serialize();
        // A malformed or unknown component must reject the entire load, not mutate the active notebook.
        QJsonObject broken=QJsonDocument::fromJson(canvas.serialize()).object();
        pages=broken.value(QStringLiteral("pages")).toArray();
        page=pages.at(0).toObject();
        QJsonArray list=page.value(QStringLiteral("symbols")).toArray();
        QJsonObject first=list.at(0).toObject();
        first.insert(QStringLiteral("id"),QStringLiteral("garbage"));
        list.replace(0,first);page.insert(QStringLiteral("symbols"),list);
        pages.replace(0,page);broken.insert(QStringLiteral("pages"),pages);
        QVERIFY(!saved.loadDocument(QJsonDocument(broken).toJson(),&error));
        QCOMPARE(saved.serialize(),before);
    }
};

QTEST_MAIN(Ink61200Test)
#include "ink_61200.moc"
