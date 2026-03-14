#include "cmd.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QMessageBox>
#include <QProcessEnvironment>
#include <QUuid>

#include <unistd.h>

Cmd::Cmd(QObject *parent)
    : QProcess(parent),
      elevate {elevationTool()},
      helper {"/usr/lib/mx-packageinstaller/helper"}
{
    connect(this, &Cmd::readyReadStandardOutput, [this] { emit outputAvailable(readAllStandardOutput()); });
    connect(this, &Cmd::readyReadStandardError, [this] { emit errorAvailable(readAllStandardError()); });
    connect(this, &Cmd::outputAvailable, [this](const QString &out) { outBuffer += out; });
    connect(this, &Cmd::errorAvailable, [this](const QString &out) { outBuffer += out; });
}

QString Cmd::elevationTool()
{
    if (QFile::exists("/usr/bin/pkexec")) return QStringLiteral("/usr/bin/pkexec");
    if (QFile::exists("/usr/bin/sudo")) return QStringLiteral("/usr/bin/sudo");
    return QStringLiteral("/usr/bin/sudo"); // fallback
}

QString Cmd::getOut(const QString &cmd, QuietMode quiet)
{
    outBuffer.clear();
    run(cmd, quiet);
    return outBuffer.trimmed();
}

QStringList Cmd::helperExecArgs(const QString &cmd, const QStringList &args) const
{
    QStringList helperArgs {"exec"};
    helperArgs << cmd;
    helperArgs += args;
    return helperArgs;
}

bool Cmd::helperProc(const QStringList &helperArgs, QString *output, const QByteArray *input, QuietMode quiet)
{
    outBuffer.clear();
    if (getuid() != 0 && elevate.isEmpty()) {
        qWarning() << "No elevation helper available";
        return false;
    }

    const QString program = (getuid() == 0) ? helper : elevate;
    QStringList programArgs = helperArgs;
    if (getuid() != 0) {
        programArgs.prepend(helper);
    }
    return startAndWait(program, programArgs, output, input, quiet, getuid() != 0);
}

bool Cmd::proc(const QString &cmd, const QStringList &args, QString *output, const QByteArray *input, QuietMode quiet)
{
    return startAndWait(cmd, args, output, input, quiet, false);
}

bool Cmd::procAsRoot(const QString &cmd, const QStringList &args, QString *output, const QByteArray *input, QuietMode quiet)
{
    return helperProc(helperExecArgs(cmd, args), output, input, quiet);
}

bool Cmd::run(const QString &cmd, QuietMode quiet)
{
    return startAndWait("/bin/bash", {"-c", cmd}, nullptr, nullptr, quiet, false);
}

bool Cmd::runHookAsRoot(const QString &script, QuietMode quiet)
{
    return helperProc({"run-hook", script}, nullptr, nullptr, quiet);
}

QString Cmd::lockingProcessAsRoot(const QString &path, QuietMode quiet)
{
    QString output;
    helperProc({"locking-process", path}, &output, nullptr, quiet);
    return output.trimmed();
}

bool Cmd::startAndWait(const QString &program, const QStringList &arguments, QString *output, const QByteArray *input,
                       QuietMode quiet, bool elevated, const QString &shellCommand)
{
    outBuffer.clear();
    helperMarkerPath.clear();
    if (state() != QProcess::NotRunning) {
        qDebug() << "Process already running:" << this->program() << this->arguments();
        return false;
    }

    if (quiet == QuietMode::No) {
        if (shellCommand.isEmpty()) {
            qDebug() << program << arguments;
        } else {
            qDebug().noquote() << shellCommand;
        }
    }

    setProcessEnvironment(QProcessEnvironment::systemEnvironment());
    if (elevated) {
        helperMarkerPath = QDir::tempPath() + QStringLiteral("/mx-pkg-helper-")
                           + QUuid::createUuid().toString(QUuid::Id128) + QStringLiteral(".marker");
        auto env = QProcessEnvironment::systemEnvironment();
        env.insert(QStringLiteral("MX_PKG_HELPER_MARKER"), helperMarkerPath);
        setProcessEnvironment(env);
    }

    QEventLoop loop;
    connect(this, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), &loop, &QEventLoop::quit);
    start(program, arguments);
    if (!waitForStarted()) {
        if (elevated) {
            setProcessEnvironment(QProcessEnvironment::systemEnvironment());
            QFile::remove(helperMarkerPath);
            helperMarkerPath.clear();
        }
        if (output) {
            *output = outBuffer.trimmed();
        }
        emit done();
        return false;
    }
    if (input && !input->isEmpty()) {
        write(*input);
    }
    closeWriteChannel();
    loop.exec();

    if (elevated) {
        setProcessEnvironment(QProcessEnvironment::systemEnvironment());
        if (isAuthenticationDismissed()) {
            handleElevationError();
        }
        QFile::remove(helperMarkerPath);
        helperMarkerPath.clear();
    }

    if (output) {
        *output = outBuffer.trimmed();
    }

    emit done();
    return (exitStatus() == QProcess::NormalExit && exitCode() == 0);
}

// Return true when process is killed or not running
bool Cmd::terminateAndKill()
{
    if (state() != QProcess::NotRunning) {
        terminate();
        if (!waitForFinished(2000)) {
            kill();
        }
    }
    return state() == QProcess::NotRunning;
}

QString Cmd::readAllOutput() const
{
    return outBuffer.trimmed();
}

bool Cmd::isAuthenticationDismissed() const
{
    if (helperMarkerPath.isEmpty()) {
        return false;
    }
    return !QFile::exists(helperMarkerPath);
}

void Cmd::handleElevationError()
{
    qWarning() << "Authentication was dismissed or failed";
}
