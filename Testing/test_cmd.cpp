#include <QTimer>
#include <QtTest>

#include <cerrno>
#include <csignal>

#include "../src/cmd.h"

class CmdTest : public QObject
{
    Q_OBJECT

private slots:
    void cancellationTerminatesProcessGroup();
};

void CmdTest::cancellationTerminatesProcessGroup()
{
    Cmd cmd;
    bool stopped = false;
    bool cancellationRequested = false;
    const auto cancel = [&cmd, &stopped, &cancellationRequested] {
        if (!cancellationRequested && cmd.state() != QProcess::NotRunning) {
            cancellationRequested = true;
            stopped = cmd.terminateAndKill();
        }
    };
    connect(&cmd, &Cmd::outputAvailable, &cmd, [&cancel](const QString &output) {
        if (!output.trimmed().isEmpty()) {
            cancel();
        }
    });
    QTimer::singleShot(1000, &cmd, cancel);

    QVERIFY(!cmd.run(QStringLiteral("sleep 30 & child=$!; echo $child; wait")));
    QVERIFY(cancellationRequested);
    QVERIFY(stopped);

    bool validPid = false;
    const qint64 childPid = cmd.readAllOutput().toLongLong(&validPid);
    QVERIFY(validPid);
    QVERIFY(childPid > 0);

    QTest::qWait(50);
    errno = 0;
    QCOMPARE(::kill(static_cast<pid_t>(childPid), 0), -1);
    QCOMPARE(errno, ESRCH);
}

QTEST_GUILESS_MAIN(CmdTest)

#include "test_cmd.moc"
