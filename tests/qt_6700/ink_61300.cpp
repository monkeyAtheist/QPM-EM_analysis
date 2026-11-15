#include "widgets/notes_ink_canvas.h"
#include <QtTest/QtTest>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QMouseEvent>
#include <QApplication>
#include <cmath>

class Ink61300Test : public QObject
{
    Q_OBJECT
private slots:
    void smartWireAnchorsFollowRotationAndSave()
    {
        NotesInkCanvas canvas;
        QVERIFY(canvas.addSymbol(QStringLiteral("resistor"),QPointF(340,320)));
        QVERIFY(canvas.addSymbol(QStringLiteral("capacitor"),QPointF(680,320)));
        QCOMPARE(canvas.symbolCount(),2);
        // Endpoints are drawn as +-70 local x for unrotated two-pin devices.
        QVERIFY(canvas.addWire(QPointF(410,320),QPointF(610,320)));
        QCOMPARE(canvas.wireCount(),1);
        QJsonObject root=QJsonDocument::fromJson(canvas.serialize()).object();
        QCOMPARE(root.value(QStringLiteral("version")).toInt(),6);
        const auto page=root.value(QStringLiteral("pages")).toArray().at(0).toObject();
        const auto wire=page.value(QStringLiteral("shapes")).toArray().at(0).toObject();
        QVERIFY(wire.value(QStringLiteral("aUid")).toDouble()>0);
        QVERIFY(wire.value(QStringLiteral("bUid")).toDouble()>0);
        QCOMPARE(wire.value(QStringLiteral("aPin")).toInt(),1);
        QCOMPARE(wire.value(QStringLiteral("bPin")).toInt(),0);
        QVERIFY(std::abs(wire.value(QStringLiteral("x1")).toDouble()-410.0)<1e-8);
        canvas.setTool(NotesInkCanvas::Tool::Select);
        canvas.resize(960,620);
        canvas.show();
        QTest::qWait(10);
        QTest::mouseClick(&canvas,Qt::LeftButton,Qt::NoModifier,
                          canvas.pageToWidget(QPointF(340,320)).toPoint());
        QVERIFY(canvas.hasSelectedSymbol());
        QVERIFY(canvas.rotateSelectedSymbol(90));
        root=QJsonDocument::fromJson(canvas.serialize()).object();
        const auto after=root.value(QStringLiteral("pages")).toArray().at(0).toObject()
                         .value(QStringLiteral("shapes")).toArray().at(0).toObject();
        // Right terminal rotated 90 degrees clockwise: (340,390).
        QVERIFY(std::abs(after.value(QStringLiteral("x1")).toDouble()-340.0)<1e-8);
        QVERIFY(std::abs(after.value(QStringLiteral("y1")).toDouble()-390.0)<1e-8);
        canvas.undo();
        root=QJsonDocument::fromJson(canvas.serialize()).object();
        const auto undone=root.value(QStringLiteral("pages")).toArray().at(0).toObject()
                          .value(QStringLiteral("shapes")).toArray().at(0).toObject();
        QVERIFY(std::abs(undone.value(QStringLiteral("x1")).toDouble()-410.0)<1e-8);
        NotesInkCanvas roundtrip;
        QString error;
        QVERIFY2(roundtrip.loadDocument(canvas.serialize(),&error),qPrintable(error));
        QCOMPARE(roundtrip.wireCount(),1);
        QCOMPARE(roundtrip.symbolCount(),2);
    }
    void multipleSelectionAndUndo()
    {
        NotesInkCanvas canvas;
        QVERIFY(canvas.addSymbol(QStringLiteral("resistor"),QPointF(350,400)));
        QVERIFY(canvas.addSymbol(QStringLiteral("inductor"),QPointF(660,400)));
        canvas.setTool(NotesInkCanvas::Tool::Select);
        canvas.selectAllObjects();
        QCOMPARE(canvas.selectedObjectCount(),2);
        canvas.resize(1000,720);
        canvas.show();
        QTest::qWait(10);
        const QPoint p1=canvas.pageToWidget(QPointF(350,400)).toPoint();
        const QPoint p2=canvas.pageToWidget(QPointF(390,440)).toPoint();
        QTest::mousePress(&canvas,Qt::LeftButton,Qt::NoModifier,p1);
        // Qt's ordinary mouseMove sends no pressed buttons: simulate a real drag.
        const QPointF local(p2);
        QMouseEvent move(QEvent::MouseMove,local,canvas.mapToGlobal(p2),
                         Qt::NoButton,Qt::LeftButton,Qt::NoModifier);
        QApplication::sendEvent(&canvas,&move);
        QTest::mouseRelease(&canvas,Qt::LeftButton,Qt::NoModifier,p2);
        QJsonObject root=QJsonDocument::fromJson(canvas.serialize()).object();
        const auto symbols=root.value(QStringLiteral("pages")).toArray().at(0).toObject()
                               .value(QStringLiteral("symbols")).toArray();
        QVERIFY(symbols.at(0).toObject().value(QStringLiteral("x")).toDouble()>350.0);
        QVERIFY(symbols.at(1).toObject().value(QStringLiteral("x")).toDouble()>660.0);
        canvas.undo();
        root=QJsonDocument::fromJson(canvas.serialize()).object();
        const auto back=root.value(QStringLiteral("pages")).toArray().at(0).toObject()
                            .value(QStringLiteral("symbols")).toArray();
        QCOMPARE(back.at(0).toObject().value(QStringLiteral("x")).toDouble(),350.0);
        QCOMPARE(back.at(1).toObject().value(QStringLiteral("x")).toDouble(),660.0);
    }
    void legacyV5AndFailedImportDontMutate()
    {
        NotesInkCanvas canvas;
        QVERIFY(canvas.addSymbol(QStringLiteral("resistor"),QPointF(400,400)));
        QJsonObject root=QJsonDocument::fromJson(canvas.serialize()).object();
        QJsonArray pages=root.value(QStringLiteral("pages")).toArray();
        QJsonObject page=pages[0].toObject();
        auto symbols=page.value(QStringLiteral("symbols")).toArray();
        QJsonObject symbol=symbols[0].toObject();
        symbol.remove(QStringLiteral("uid"));
        symbols.replace(0,symbol);page.insert(QStringLiteral("symbols"),symbols);
        pages.replace(0,page);root.insert(QStringLiteral("pages"),pages);
        root.insert(QStringLiteral("version"),5);
        QString error;
        NotesInkCanvas migrated;
        QVERIFY2(migrated.loadDocument(QJsonDocument(root).toJson(),&error),qPrintable(error));
        QCOMPARE(migrated.symbolCount(),1);
        QCOMPARE(QJsonDocument::fromJson(migrated.serialize()).object()
                    .value(QStringLiteral("version")).toInt(),6);
        const QByteArray before=migrated.serialize();
        root.insert(QStringLiteral("version"),999);
        QVERIFY(!migrated.loadDocument(QJsonDocument(root).toJson(),&error));
        QCOMPARE(migrated.serialize(),before);
    }
};
QTEST_MAIN(Ink61300Test)
#include "ink_61300.moc"
