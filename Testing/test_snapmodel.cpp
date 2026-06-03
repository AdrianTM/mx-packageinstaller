#include <QtTest>
#include <QSignalSpy>
#include "../src/models/snapmodel.h"

// Use the same Status values as packagestatus.h
namespace SnapStatus
{
enum { Installed = 1, Upgradable, NotInstalled, Autoremovable };
}

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

    // Check state tests
    void testCheckStateChange();
    void testCheckStateSignal();
    void testSetAllChecked();
    void testCheckedPackages();

    // Lookup tests
    void testFindSnapRow();
    void testSnapAt();
    void testSnapAtInvalidRow();
    void testIsClassic();

    // Status tests
    void testUpdateInstalledStatus();

    // Model reset tests
    void testClear();

    // Flags tests
    void testItemFlags();

    // Header tests
    void testHeaderData();

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
    s1.status = SnapStatus::Installed;
    snaps.append(s1);

    SnapData s2;
    s2.name = "code";
    s2.version = "1.86.0";
    s2.publisher = "vscode";
    s2.notes = "classic";
    s2.description = "Code editing. Redefined.";
    s2.isClassic = true;
    s2.status = SnapStatus::NotInstalled;
    snaps.append(s2);

    SnapData s3;
    s3.name = "vlc";
    s3.version = "3.0.20";
    s3.publisher = "videolan";
    s3.notes = "-";
    s3.description = "The ultimate media player";
    s3.status = SnapStatus::NotInstalled;
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
    QCOMPARE(model.data(model.index(0, SnapCol::Status), Qt::UserRole).toInt(), SnapStatus::Installed);

    // Classic column
    QCOMPARE(model.data(model.index(1, SnapCol::Classic), Qt::UserRole).toBool(), true);
    QCOMPARE(model.data(model.index(2, SnapCol::Classic), Qt::UserRole).toBool(), false);

    // Name via UserRole
    QCOMPARE(model.data(model.index(2, SnapCol::Name), Qt::UserRole).toString(), QString("vlc"));
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
    QCOMPARE(args.at(2).toInt(), SnapStatus::NotInstalled);
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
    QCOMPARE(model.snapAt(0)->status, SnapStatus::Installed);
    QCOMPARE(model.snapAt(1)->status, SnapStatus::NotInstalled);
    QCOMPARE(model.snapAt(2)->status, SnapStatus::NotInstalled);

    // After update: vlc installed, core no longer installed
    model.updateInstalledStatus({"vlc"});

    QCOMPARE(model.snapAt(0)->status, SnapStatus::NotInstalled);
    QCOMPARE(model.snapAt(1)->status, SnapStatus::NotInstalled);
    QCOMPARE(model.snapAt(2)->status, SnapStatus::Installed);
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

QTEST_MAIN(TestSnapModel)
#include "test_snapmodel.moc"
