#include "antenna_project_document.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtTest>

class ProjectDocumentTest : public QObject
{
    Q_OBJECT
private:
    static QJsonObject readFixture(const QString &name)
    {
        QFile file(QStringLiteral(QTA_FIXTURE_DIR) + QLatin1Char('/') + name);
        if (!file.open(QIODevice::ReadOnly)) return {};
        QJsonParseError error;
        auto doc = QJsonDocument::fromJson(file.readAll(), &error);
        return error.error == QJsonParseError::NoError ? doc.object() : QJsonObject();
    }
private slots:
    void validV19Roundtrip()
    {
        auto root = readFixture(QStringLiteral("qta_v19_roundtrip.json"));
        QVERIFY(!root.isEmpty());
        QString notice, error;
        int version = 0;
        QVERIFY2(AntennaProjectDocument::checkAntennaProjectSchema(root, version, notice, error), qPrintable(error));
        QCOMPARE(version, 19);
        root = AntennaProjectDocument::migrateAntennaProjectToCurrent(root, version);
        QVERIFY2(AntennaProjectDocument::validateAntennaProjectDocument(root, error), qPrintable(error));
        QCOMPARE(root.value(QStringLiteral("version")).toInt(), 19);
    }
    void legacyMigration()
    {
        const auto legacy = readFixture(QStringLiteral("qta_legacy_unversioned.json"));
        QVERIFY(!legacy.isEmpty());
        QString notice, error;
        int version = 0;
        QVERIFY2(AntennaProjectDocument::checkAntennaProjectSchema(legacy, version, notice, error), qPrintable(error));
        QCOMPARE(version, 1);
        QVERIFY(!notice.isEmpty());
        const auto current = AntennaProjectDocument::migrateAntennaProjectToCurrent(legacy, version);
        QVERIFY2(AntennaProjectDocument::validateAntennaProjectDocument(current, error), qPrintable(error));
        QCOMPARE(current.value(QStringLiteral("units")).toString(), QStringLiteral("m"));
    }
    void rejectsFutureAndMalformed()
    {
        QString notice, error;
        int version = 0;
        QVERIFY(!AntennaProjectDocument::checkAntennaProjectSchema(
            readFixture(QStringLiteral("qta_future_v20.json")), version, notice, error));
        QVERIFY(!error.isEmpty());
        error.clear();
        QVERIFY(!AntennaProjectDocument::validateAntennaProjectDocument(
            readFixture(QStringLiteral("qta_invalid_zero_length_wire.json")), error));
        QVERIFY(!error.isEmpty());
    }
    void rejectsHugeVersionWithoutNarrowing()
    {
        QJsonObject root{{QStringLiteral("version"), 1e100}};
        int version = 0;
        QString notice, error;
        QVERIFY(!AntennaProjectDocument::checkAntennaProjectSchema(root, version, notice, error));
        QVERIFY(!error.isEmpty());
    }
};
QTEST_GUILESS_MAIN(ProjectDocumentTest)
#include "project_document_6700.moc"
