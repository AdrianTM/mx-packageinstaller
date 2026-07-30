#include <QTimer>
#include <QtTest>

#include <cerrno>
#include <csignal>

#include "src/cmd.h"

class CmdTest : public QObject
{
    Q_OBJECT

private slots:
    void cancellationTerminatesProcessGroup();
    void authDismissalClassification();
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

// Pure classification logic behind Cmd::isAuthenticationDismissed() -- see
// Cmd::classifyAuthDismissed() in src/cmd.cpp. This is what lets the GUI tell
// "the user dismissed the polkit prompt" (revert quietly) apart from "the
// elevated command itself genuinely exited 126/127" (report a real failure),
// without ever needing to actually invoke pkexec.
void CmdTest::authDismissalClassification()
{
    // Dismissed: pkexec's own 126/127 exit, and the helper never got to create
    // its marker because it was never authorized to run.
    QVERIFY(Cmd::classifyAuthDismissed(QProcess::NormalExit, 126, /*markerPathEmpty=*/false, /*markerExists=*/false));
    QVERIFY(Cmd::classifyAuthDismissed(QProcess::NormalExit, 127, /*markerPathEmpty=*/false, /*markerExists=*/false));

    // Not dismissed: same exit code, but the marker exists -- the helper did
    // run (auth succeeded) and the command itself is what exited 126/127.
    QVERIFY(!Cmd::classifyAuthDismissed(QProcess::NormalExit, 126, /*markerPathEmpty=*/false, /*markerExists=*/true));
    QVERIFY(!Cmd::classifyAuthDismissed(QProcess::NormalExit, 127, /*markerPathEmpty=*/false, /*markerExists=*/true));

    // Not dismissed: an unrelated exit code, regardless of the marker.
    QVERIFY(!Cmd::classifyAuthDismissed(QProcess::NormalExit, 1, /*markerPathEmpty=*/false, /*markerExists=*/false));
    QVERIFY(!Cmd::classifyAuthDismissed(QProcess::NormalExit, 0, /*markerPathEmpty=*/false, /*markerExists=*/false));

    // Not dismissed: a crash rather than a normal exit.
    QVERIFY(!Cmd::classifyAuthDismissed(QProcess::CrashExit, 126, /*markerPathEmpty=*/false, /*markerExists=*/false));

    // Not dismissed: this call was never elevated in the first place (no
    // marker path was ever set up), regardless of exit code.
    QVERIFY(!Cmd::classifyAuthDismissed(QProcess::NormalExit, 126, /*markerPathEmpty=*/true, /*markerExists=*/false));
}

QTEST_GUILESS_MAIN(CmdTest)

#include "test_cmd.moc"
