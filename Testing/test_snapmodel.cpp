#include <QtTest>
#include <QSignalSpy>
#include "../src/models/packagemodel.h"
#include "../src/models/snapmodel.h"

class TestSnapModel : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();

    // Basic model tests
    void testEmptyModel();
    void testSetSnapData();
    void testRowColumnCount();

    // Data retrieval tests
    void testDataDisplayRole();
    void testDataCheckStateRole();
    void testDataUserRole();
    void testInvalidDataRequests();

    // Check state tests
    void testCheckStateChange();
    void testCheckStateSignal();
    void testRejectedSetDataDoesNotEmit();
    void testSetAllChecked();
    void testSetAllCheckedSignal();
    void testCheckedPackages();

    // Lookup tests
    void testFindSnapRow();
    void testSetSnapDataRebuildsNameLookup();
    void testSnapAt();
    void testSnapAtInvalidRow();
    void testIsClassic();

    // Status tests
    void testUpdateInstalledStatus();
    void testUpdateInstalledStatusSignal();
    void testDecorationRole();

    // Model reset tests
    void testClear();

    // Flags tests
    void testItemFlags();

    // Header tests
    void testHeaderData();

    // parseSnapList() tests
    void testParseInstalledList();
    void testParseFindList();
    void testParseSkipsHeaderAndBlankLines();
    void testParseSkipsMalformedRows();
    void testParseStripsPublisherMarkers();
    void testParseDetectsClassic();
    void testParseInstalledMissingOptionalColumns();
    void testParseEmptyOutput();

private:
    QVector<SnapData> createTestSnaps();
};

void TestSnapModel::initTestCase()
{
    qDebug() << "Starting SnapModel tests";
}

void TestSnapModel::cleanupTestCase()
{
    qDebug() << "Finished SnapModel tests";
}

QVector<SnapData> TestSnapModel::createTestSnaps()
{
    QVector<SnapData> snaps;

    SnapData s1;
    s1.name = "core";
    s1.version = "16-2.61";
    s1.publisher = "canonical";
    s1.notes = "core";
    s1.description = "snapd runtime environment";
    s1.status = Status::Installed;
    snaps.append(s1);

    SnapData s2;
    s2.name = "code";
    s2.version = "1.86.0";
    s2.publisher = "vscode";
    s2.notes = "classic";
    s2.description = "Code editing. Redefined.";
    s2.isClassic = true;
    s2.status = Status::NotInstalled;
    snaps.append(s2);

    SnapData s3;
    s3.name = "vlc";
    s3.version = "3.0.20";
    s3.publisher = "videolan";
    s3.notes = "-";
    s3.description = "The ultimate media player";
    s3.status = Status::NotInstalled;
    snaps.append(s3);

    return snaps;
}

void TestSnapModel::testEmptyModel()
{
    SnapModel model;
    QCOMPARE(model.rowCount(), 0);
    QCOMPARE(model.columnCount(), SnapCol::Classic + 1);
    QVERIFY(model.checkedPackages().isEmpty());
}

void TestSnapModel::testSetSnapData()
{
    SnapModel model;
    model.setSnapData(createTestSnaps());
    QCOMPARE(model.rowCount(), 3);
}

void TestSnapModel::testRowColumnCount()
{
    SnapModel model;
    model.setSnapData(createTestSnaps());

    QCOMPARE(model.rowCount(), 3);
    QCOMPARE(model.columnCount(), SnapCol::Classic + 1);

    // Parent should return 0 for table models
    QModelIndex parent = model.index(0, 0);
    QCOMPARE(model.rowCount(parent), 0);
    QCOMPARE(model.columnCount(parent), 0);
}

void TestSnapModel::testDataDisplayRole()
{
    SnapModel model;
    model.setSnapData(createTestSnaps());

    QCOMPARE(model.data(model.index(0, SnapCol::Name), Qt::DisplayRole).toString(), QString("core"));
    QCOMPARE(model.data(model.index(0, SnapCol::Version), Qt::DisplayRole).toString(), QString("16-2.61"));
    QCOMPARE(model.data(model.index(0, SnapCol::Publisher), Qt::DisplayRole).toString(), QString("canonical"));
    QCOMPARE(model.data(model.index(0, SnapCol::Notes), Qt::DisplayRole).toString(), QString("core"));
    QCOMPARE(model.data(model.index(0, SnapCol::Description), Qt::DisplayRole).toString(),
             QString("snapd runtime environment"));
}

void TestSnapModel::testDataCheckStateRole()
{
    SnapModel model;
    model.setSnapData(createTestSnaps());

    QModelIndex checkIndex = model.index(0, SnapCol::Check);
    QCOMPARE(model.data(checkIndex, Qt::CheckStateRole).toInt(), static_cast<int>(Qt::Unchecked));

    // Non-check column should return empty
    QModelIndex nameIndex = model.index(0, SnapCol::Name);
    QVERIFY(!model.data(nameIndex, Qt::CheckStateRole).isValid());
}

void TestSnapModel::testDataUserRole()
{
    SnapModel model;
    model.setSnapData(createTestSnaps());

    // Status column
    QCOMPARE(model.data(model.index(0, SnapCol::Status), Qt::UserRole).toInt(), Status::Installed);

    // Classic column
    QCOMPARE(model.data(model.index(1, SnapCol::Classic), Qt::UserRole).toBool(), true);
    QCOMPARE(model.data(model.index(2, SnapCol::Classic), Qt::UserRole).toBool(), false);

    // Name via UserRole
    QCOMPARE(model.data(model.index(2, SnapCol::Name), Qt::UserRole).toString(), QString("vlc"));
}

void TestSnapModel::testInvalidDataRequests()
{
    SnapModel model;
    model.setSnapData(createTestSnaps());

    QVERIFY(!model.data(QModelIndex(), Qt::DisplayRole).isValid());
    QVERIFY(!model.data(model.index(model.rowCount(), SnapCol::Name), Qt::DisplayRole).isValid());
    QVERIFY(!model.data(model.index(0, SnapCol::Check), Qt::DisplayRole).isValid());
    QVERIFY(!model.data(model.index(0, SnapCol::Status), Qt::DisplayRole).isValid());
    QVERIFY(!model.data(model.index(0, SnapCol::Publisher), Qt::UserRole).isValid());
}

void TestSnapModel::testCheckStateChange()
{
    SnapModel model;
    model.setSnapData(createTestSnaps());

    QModelIndex checkIndex = model.index(0, SnapCol::Check);
    QCOMPARE(model.data(checkIndex, Qt::CheckStateRole).toInt(), static_cast<int>(Qt::Unchecked));

    QVERIFY(model.setData(checkIndex, Qt::Checked, Qt::CheckStateRole));
    QCOMPARE(model.data(checkIndex, Qt::CheckStateRole).toInt(), static_cast<int>(Qt::Checked));

    QVERIFY(model.setData(checkIndex, Qt::Unchecked, Qt::CheckStateRole));
    QCOMPARE(model.data(checkIndex, Qt::CheckStateRole).toInt(), static_cast<int>(Qt::Unchecked));
}

void TestSnapModel::testCheckStateSignal()
{
    SnapModel model;
    model.setSnapData(createTestSnaps());

    QSignalSpy spy(&model, &SnapModel::checkStateChanged);
    QVERIFY(spy.isValid());

    model.setData(model.index(2, SnapCol::Check), Qt::Checked, Qt::CheckStateRole);

    QCOMPARE(spy.count(), 1);
    QList<QVariant> args = spy.takeFirst();
    QCOMPARE(args.at(0).toString(), QString("vlc"));
    QCOMPARE(args.at(1).value<Qt::CheckState>(), Qt::Checked);
    QCOMPARE(args.at(2).toInt(), Status::NotInstalled);
}

void TestSnapModel::testRejectedSetDataDoesNotEmit()
{
    SnapModel model;
    model.setSnapData(createTestSnaps());

    QSignalSpy checkSpy(&model, &SnapModel::checkStateChanged);
    QSignalSpy dataSpy(&model, &QAbstractItemModel::dataChanged);
    QVERIFY(checkSpy.isValid());
    QVERIFY(dataSpy.isValid());

    const QModelIndex checkIndex = model.index(1, SnapCol::Check);
    QVERIFY(!model.setData(model.index(1, SnapCol::Name), Qt::Checked, Qt::CheckStateRole));
    QVERIFY(!model.setData(checkIndex, Qt::Checked, Qt::DisplayRole));
    QVERIFY(!model.setData(QModelIndex(), Qt::Checked, Qt::CheckStateRole));
    QCOMPARE(checkSpy.count(), 0);
    QCOMPARE(dataSpy.count(), 0);

    QVERIFY(model.setData(checkIndex, Qt::Checked, Qt::CheckStateRole));
    QCOMPARE(checkSpy.count(), 1);
    QCOMPARE(dataSpy.count(), 1);

    QVERIFY(!model.setData(checkIndex, Qt::Checked, Qt::CheckStateRole));
    QCOMPARE(checkSpy.count(), 1);
    QCOMPARE(dataSpy.count(), 1);
}

void TestSnapModel::testSetAllChecked()
{
    SnapModel model;
    model.setSnapData(createTestSnaps());

    model.setAllChecked(true);
    for (int i = 0; i < model.rowCount(); ++i) {
        QCOMPARE(model.data(model.index(i, SnapCol::Check), Qt::CheckStateRole).toInt(),
                 static_cast<int>(Qt::Checked));
    }

    model.setAllChecked(false);
    for (int i = 0; i < model.rowCount(); ++i) {
        QCOMPARE(model.data(model.index(i, SnapCol::Check), Qt::CheckStateRole).toInt(),
                 static_cast<int>(Qt::Unchecked));
    }
}

void TestSnapModel::testSetAllCheckedSignal()
{
    SnapModel model;
    model.setSnapData(createTestSnaps());

    QSignalSpy checkSpy(&model, &SnapModel::checkStateChanged);
    QSignalSpy dataSpy(&model, &QAbstractItemModel::dataChanged);
    QVERIFY(checkSpy.isValid());
    QVERIFY(dataSpy.isValid());

    model.setAllChecked(true);

    QCOMPARE(checkSpy.count(), 0);
    QCOMPARE(dataSpy.count(), 1);
    const QList<QVariant> args = dataSpy.takeFirst();
    QCOMPARE(args.at(0).toModelIndex(), model.index(0, SnapCol::Check));
    QCOMPARE(args.at(1).toModelIndex(), model.index(model.rowCount() - 1, SnapCol::Check));
    QCOMPARE(args.at(2).value<QList<int>>(), QList<int>({Qt::CheckStateRole}));
}

void TestSnapModel::testCheckedPackages()
{
    SnapModel model;
    model.setSnapData(createTestSnaps());

    QVERIFY(model.checkedPackages().isEmpty());

    model.setData(model.index(1, SnapCol::Check), Qt::Checked, Qt::CheckStateRole);
    model.setData(model.index(2, SnapCol::Check), Qt::Checked, Qt::CheckStateRole);

    QStringList checked = model.checkedPackages();
    QCOMPARE(checked.size(), 2);
    QVERIFY(checked.contains("code"));
    QVERIFY(checked.contains("vlc"));
    QVERIFY(!checked.contains("core"));
}

void TestSnapModel::testFindSnapRow()
{
    SnapModel model;
    model.setSnapData(createTestSnaps());

    QCOMPARE(model.findSnapRow("core"), 0);
    QCOMPARE(model.findSnapRow("code"), 1);
    QCOMPARE(model.findSnapRow("vlc"), 2);
    QCOMPARE(model.findSnapRow("nonexistent"), -1);
}

void TestSnapModel::testSetSnapDataRebuildsNameLookup()
{
    SnapModel model;
    model.setSnapData(createTestSnaps());
    QCOMPARE(model.findSnapRow("core"), 0);
    QVERIFY(model.isClassic("code"));

    SnapData replacement;
    replacement.name = "only-one";
    replacement.version = "1";
    replacement.status = Status::NotInstalled;
    model.setSnapData({replacement});

    QCOMPARE(model.rowCount(), 1);
    QCOMPARE(model.findSnapRow("core"), -1);
    QCOMPARE(model.findSnapRow("code"), -1);
    QCOMPARE(model.findSnapRow("only-one"), 0);
    QVERIFY(!model.isClassic("code"));
}

void TestSnapModel::testSnapAt()
{
    SnapModel model;
    model.setSnapData(createTestSnaps());

    const SnapData *s = model.snapAt(0);
    QVERIFY(s != nullptr);
    QCOMPARE(s->name, QString("core"));

    const SnapData *s2 = model.snapAt(2);
    QVERIFY(s2 != nullptr);
    QCOMPARE(s2->name, QString("vlc"));
}

void TestSnapModel::testSnapAtInvalidRow()
{
    SnapModel model;
    model.setSnapData(createTestSnaps());

    QVERIFY(model.snapAt(-1) == nullptr);
    QVERIFY(model.snapAt(100) == nullptr);
}

void TestSnapModel::testIsClassic()
{
    SnapModel model;
    model.setSnapData(createTestSnaps());

    QVERIFY(model.isClassic("code"));    // classic snap
    QVERIFY(!model.isClassic("vlc"));     // strict snap
    QVERIFY(!model.isClassic("core"));    // base
    QVERIFY(!model.isClassic("missing")); // unknown -> false
}

void TestSnapModel::testUpdateInstalledStatus()
{
    SnapModel model;
    model.setSnapData(createTestSnaps());

    // Initially: core installed, code/vlc not installed
    QCOMPARE(model.snapAt(0)->status, Status::Installed);
    QCOMPARE(model.snapAt(1)->status, Status::NotInstalled);
    QCOMPARE(model.snapAt(2)->status, Status::NotInstalled);

    // After update: vlc installed, core no longer installed
    model.updateInstalledStatus({"vlc"});

    QCOMPARE(model.snapAt(0)->status, Status::NotInstalled);
    QCOMPARE(model.snapAt(1)->status, Status::NotInstalled);
    QCOMPARE(model.snapAt(2)->status, Status::Installed);
}

void TestSnapModel::testUpdateInstalledStatusSignal()
{
    SnapModel model;
    model.setSnapData(createTestSnaps());

    QSignalSpy dataSpy(&model, &QAbstractItemModel::dataChanged);
    QVERIFY(dataSpy.isValid());

    model.updateInstalledStatus({"core", "vlc"});
    QCOMPARE(dataSpy.count(), 1);
    QList<QVariant> args = dataSpy.takeFirst();
    QCOMPARE(args.at(0).toModelIndex(), model.index(2, SnapCol::Check));
    QCOMPARE(args.at(1).toModelIndex(), model.index(2, SnapCol::Status));

    model.updateInstalledStatus({"core", "vlc"});
    QCOMPARE(dataSpy.count(), 0);

    model.updateInstalledStatus({});
    QCOMPARE(dataSpy.count(), 2);
}

void TestSnapModel::testDecorationRole()
{
    SnapModel model;
    model.setSnapData(createTestSnaps());

    QVERIFY(model.data(model.index(0, SnapCol::Check), Qt::DecorationRole).value<QIcon>().isNull());

    QPixmap pixmap(8, 8);
    pixmap.fill(Qt::green);
    model.setIcons(QIcon(pixmap));

    QVERIFY(!model.data(model.index(0, SnapCol::Check), Qt::DecorationRole).value<QIcon>().isNull());
    QVERIFY(!model.data(model.index(1, SnapCol::Check), Qt::DecorationRole).isValid());
    QVERIFY(!model.data(model.index(0, SnapCol::Name), Qt::DecorationRole).isValid());
}

void TestSnapModel::testClear()
{
    SnapModel model;
    model.setSnapData(createTestSnaps());

    QCOMPARE(model.rowCount(), 3);

    model.clear();

    QCOMPARE(model.rowCount(), 0);
    QVERIFY(model.checkedPackages().isEmpty());
    QCOMPARE(model.findSnapRow("core"), -1);
}

void TestSnapModel::testItemFlags()
{
    SnapModel model;
    model.setSnapData(createTestSnaps());

    QModelIndex checkIndex = model.index(0, SnapCol::Check);
    Qt::ItemFlags checkFlags = model.flags(checkIndex);
    QVERIFY(checkFlags & Qt::ItemIsEnabled);
    QVERIFY(checkFlags & Qt::ItemIsSelectable);
    QVERIFY(checkFlags & Qt::ItemIsUserCheckable);

    QModelIndex nameIndex = model.index(0, SnapCol::Name);
    Qt::ItemFlags nameFlags = model.flags(nameIndex);
    QVERIFY(nameFlags & Qt::ItemIsEnabled);
    QVERIFY(nameFlags & Qt::ItemIsSelectable);
    QVERIFY(!(nameFlags & Qt::ItemIsUserCheckable));

    QModelIndex invalidIndex;
    QCOMPARE(model.flags(invalidIndex), Qt::NoItemFlags);
}

void TestSnapModel::testHeaderData()
{
    SnapModel model;

    QCOMPARE(model.headerData(SnapCol::Name, Qt::Horizontal, Qt::DisplayRole).toString(), QString("Package"));
    QCOMPARE(model.headerData(SnapCol::Version, Qt::Horizontal, Qt::DisplayRole).toString(), QString("Version"));
    QCOMPARE(model.headerData(SnapCol::Publisher, Qt::Horizontal, Qt::DisplayRole).toString(), QString("Publisher"));
    QCOMPARE(model.headerData(SnapCol::Notes, Qt::Horizontal, Qt::DisplayRole).toString(), QString("Notes"));
    QCOMPARE(model.headerData(SnapCol::Description, Qt::Horizontal, Qt::DisplayRole).toString(),
             QString("Description"));

    // Check column should have empty header
    QVERIFY(model.headerData(SnapCol::Check, Qt::Horizontal, Qt::DisplayRole).toString().isEmpty());

    // Vertical orientation should return empty
    QVERIFY(!model.headerData(0, Qt::Vertical, Qt::DisplayRole).isValid());
}

void TestSnapModel::testParseInstalledList()
{
    // Columns: Name Version Rev Tracking Publisher Notes
    const QString output = QStringLiteral(
        "Name   Version    Rev    Tracking       Publisher   Notes\n"
        "core   16-2.61.4  16928  latest/stable  canonical   core\n"
        "hello  2.10       42     latest/stable  canonical   -\n");

    const QVector<SnapData> snaps = parseSnapList(output, true);
    QCOMPARE(snaps.size(), 2);

    QCOMPARE(snaps.at(0).name, QString("core"));
    QCOMPARE(snaps.at(0).version, QString("16-2.61.4"));
    QCOMPARE(snaps.at(0).publisher, QString("canonical"));
    QCOMPARE(snaps.at(0).notes, QString("core"));
    // Installed parsing does not populate a description.
    QVERIFY(snaps.at(0).description.isEmpty());

    QCOMPARE(snaps.at(1).name, QString("hello"));
    QCOMPARE(snaps.at(1).notes, QString("-"));
}

void TestSnapModel::testParseFindList()
{
    // Columns: Name Version Publisher Notes Summary...
    const QString output = QStringLiteral(
        "Name   Version  Publisher  Notes  Summary\n"
        "hello  2.10     canonical  -      GNU Hello, the hello world snap\n"
        "vlc    3.0.18   videolan   -      The ultimate media player\n");

    const QVector<SnapData> snaps = parseSnapList(output, false);
    QCOMPARE(snaps.size(), 2);

    QCOMPARE(snaps.at(0).name, QString("hello"));
    QCOMPARE(snaps.at(0).version, QString("2.10"));
    QCOMPARE(snaps.at(0).publisher, QString("canonical"));
    QCOMPARE(snaps.at(0).notes, QString("-"));
    // The summary (column 5+) is collapsed into the description.
    QCOMPARE(snaps.at(0).description, QString("GNU Hello, the hello world snap"));

    QCOMPARE(snaps.at(1).name, QString("vlc"));
    QCOMPARE(snaps.at(1).description, QString("The ultimate media player"));
}

void TestSnapModel::testParseSkipsHeaderAndBlankLines()
{
    // Header row (starts with "Name") and blank lines must be skipped.
    const QString output = QStringLiteral(
        "Name   Version  Publisher  Notes  Summary\n"
        "\n"
        "hello  2.10     canonical  -      A snap\n"
        "\n");

    const QVector<SnapData> snaps = parseSnapList(output, false);
    QCOMPARE(snaps.size(), 1);
    QCOMPARE(snaps.at(0).name, QString("hello"));
}

void TestSnapModel::testParseSkipsMalformedRows()
{
    // find rows need at least 4 columns; installed rows at least 2.
    const QString findOutput = QStringLiteral(
        "hello  2.10  canonical\n"          // only 3 columns -> skipped
        "vlc    3.0.18  videolan  -  Media\n");
    const QVector<SnapData> found = parseSnapList(findOutput, false);
    QCOMPARE(found.size(), 1);
    QCOMPARE(found.at(0).name, QString("vlc"));

    const QString installedOutput = QStringLiteral(
        "lonely\n"                          // only 1 column -> skipped
        "core  16-2.61  16928  latest/stable  canonical  core\n");
    const QVector<SnapData> installed = parseSnapList(installedOutput, true);
    QCOMPARE(installed.size(), 1);
    QCOMPARE(installed.at(0).name, QString("core"));
}

void TestSnapModel::testParseStripsPublisherMarkers()
{
    // snap appends a checkmark (U+2713) to verified publishers and trailing
    // asterisks to starred ones; both must be stripped from the publisher field.
    const QString output = QStringLiteral(
        "Name   Version  Publisher        Notes  Summary\n"
        "a      1.0      canonical✓   -      verified publisher\n"
        "b      1.0      acme**           -      starred publisher\n"
        "c      1.0      both✓**      -      verified and starred\n");

    const QVector<SnapData> snaps = parseSnapList(output, false);
    QCOMPARE(snaps.size(), 3);
    QCOMPARE(snaps.at(0).publisher, QString("canonical"));
    QCOMPARE(snaps.at(1).publisher, QString("acme"));
    QCOMPARE(snaps.at(2).publisher, QString("both"));
}

void TestSnapModel::testParseDetectsClassic()
{
    // isClassic is derived from the Notes column containing "classic".
    const QString output = QStringLiteral(
        "Name   Version  Publisher  Notes    Summary\n"
        "code   1.2.3    vscode     classic  Code editor\n"
        "hello  2.10     canonical  -        Hello\n");

    const QVector<SnapData> snaps = parseSnapList(output, false);
    QCOMPARE(snaps.size(), 2);
    QVERIFY(snaps.at(0).isClassic);
    QVERIFY(!snaps.at(1).isClassic);
}

void TestSnapModel::testParseInstalledMissingOptionalColumns()
{
    // A row with only Name and Version (e.g. odd snapd output) must still parse,
    // leaving the optional publisher/notes fields empty rather than crashing.
    const QString output = QStringLiteral("minimal  1.0\n");
    const QVector<SnapData> snaps = parseSnapList(output, true);
    QCOMPARE(snaps.size(), 1);
    QCOMPARE(snaps.at(0).name, QString("minimal"));
    QCOMPARE(snaps.at(0).version, QString("1.0"));
    QVERIFY(snaps.at(0).publisher.isEmpty());
    QVERIFY(snaps.at(0).notes.isEmpty());
}

void TestSnapModel::testParseEmptyOutput()
{
    QVERIFY(parseSnapList(QString(), true).isEmpty());
    QVERIFY(parseSnapList(QString(), false).isEmpty());
    // Header-only output yields no rows.
    QVERIFY(parseSnapList(QStringLiteral("Name  Version  Publisher  Notes  Summary\n"), false).isEmpty());
}

QTEST_MAIN(TestSnapModel)
#include "test_snapmodel.moc"
