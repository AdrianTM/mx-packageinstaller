#include <QtTest>

#include "../src/sizeutils.h"

class TestSizeUtils : public QObject
{
    Q_OBJECT

private slots:
    void testSizeStringToBytes();
    void testSortOrderAcrossUnits();
};

void TestSizeUtils::testSizeStringToBytes()
{
    QCOMPARE(SizeUtils::sizeStringToBytes("500 kB"), 500ULL * 1000ULL);
    QCOMPARE(SizeUtils::sizeStringToBytes("1,0 MB"), 1000ULL * 1000ULL);
    QCOMPARE(SizeUtils::sizeStringToBytes(QString("1,0") + QChar(0x00a0) + "GB"),
             1000ULL * 1000ULL * 1000ULL);
    QCOMPARE(SizeUtils::sizeStringToBytes("1.1 GB"),
             static_cast<quint64>(1.1 * 1000.0 * 1000.0 * 1000.0));
    QCOMPARE(SizeUtils::sizeStringToBytes("1 MiB"), 1024ULL * 1024ULL);
    QCOMPARE(SizeUtils::sizeStringToBytes("42 bytes"), 42ULL);
    QCOMPARE(SizeUtils::sizeStringToBytes(""), 0ULL);
    QCOMPARE(SizeUtils::sizeStringToBytes("?"), 0ULL);
}

void TestSizeUtils::testSortOrderAcrossUnits()
{
    const QStringList ascending {"500 kB", "1,0 MB", "100,0 MB", "1,0 GB", "1,1 GB"};
    for (int i = 1; i < ascending.size(); ++i) {
        QVERIFY2(SizeUtils::sizeStringToBytes(ascending.at(i - 1))
                     < SizeUtils::sizeStringToBytes(ascending.at(i)),
                 qPrintable(ascending.at(i - 1) + " should be smaller than " + ascending.at(i)));
    }
}

QTEST_MAIN(TestSizeUtils)
#include "test_sizeutils.moc"
