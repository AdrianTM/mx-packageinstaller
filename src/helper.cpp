/**********************************************************************
 *  helper.cpp
 **********************************************************************
 * Copyright (C) 2026 MX Authors
 *
 * Authors: Adrian
 *          MX Linux <http://mxlinux.org>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this package. If not, see <http://www.gnu.org/licenses/>.
 **********************************************************************/

#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QSet>

#include <cstdio>

namespace
{
struct ProcessResult
{
    bool started = false;
    int exitCode = 1;
    QProcess::ExitStatus exitStatus = QProcess::NormalExit;
    QByteArray standardOutput;
    QByteArray standardError;
};

constexpr auto MxpiLibPath = "/usr/lib/mx-packageinstaller/mxpi-lib";

void writeAndFlush(FILE *stream, const QByteArray &data)
{
    if (!data.isEmpty()) {
        std::fwrite(data.constData(), 1, static_cast<size_t>(data.size()), stream);
        std::fflush(stream);
    }
}

void printError(const QString &message)
{
    writeAndFlush(stderr, message.toUtf8() + '\n');
}

[[nodiscard]] const QHash<QString, QStringList> &allowedCommands()
{
    static const QHash<QString, QStringList> commands {
        {"chown", {"/usr/bin/chown", "/bin/chown"}},
        {"fuser", {"/usr/bin/fuser", "/bin/fuser"}},
        {"mxpi-lib", {MxpiLibPath}},
        {"pacman", {"/usr/bin/pacman"}},
        {"ps", {"/usr/bin/ps", "/bin/ps"}},
    };
    return commands;
}

[[nodiscard]] QString resolveBinary(const QStringList &candidates)
{
    for (const QString &candidate : candidates) {
        const QFileInfo info(candidate);
        if (info.exists() && info.isExecutable()) {
            return candidate;
        }
    }
    return {};
}

[[nodiscard]] ProcessResult runProcess(const QString &program, const QStringList &args)
{
    ProcessResult result;

    QProcess process;
    process.setProcessEnvironment(QProcessEnvironment::systemEnvironment());
    process.start(program, args, QIODevice::ReadWrite);
    if (!process.waitForStarted()) {
        result.standardError = QString("Failed to start %1").arg(program).toUtf8();
        result.exitCode = 127;
        return result;
    }

    result.started = true;
    process.closeWriteChannel();
    process.waitForFinished(-1);
    result.exitStatus = process.exitStatus();
    result.exitCode = process.exitCode();
    result.standardOutput = process.readAllStandardOutput();
    result.standardError = process.readAllStandardError();
    return result;
}

// Run with stdin/stdout/stderr forwarded directly to the parent process,
// allowing real-time interactive I/O (e.g. pacman prompts).
[[nodiscard]] int runProcessInteractive(const QString &program, const QStringList &args)
{
    QProcess process;
    process.setProcessEnvironment(QProcessEnvironment::systemEnvironment());
    process.setInputChannelMode(QProcess::ForwardedInputChannel);
    process.setProcessChannelMode(QProcess::ForwardedChannels);
    process.start(program, args);
    if (!process.waitForStarted()) {
        printError(QString("Failed to start %1").arg(program));
        return 127;
    }

    process.waitForFinished(-1);
    if (process.exitStatus() != QProcess::NormalExit) {
        return 1;
    }
    return process.exitCode();
}

[[nodiscard]] int relayResult(const ProcessResult &result)
{
    writeAndFlush(stdout, result.standardOutput);
    writeAndFlush(stderr, result.standardError);
    if (!result.started) {
        return result.exitCode;
    }
    return result.exitStatus == QProcess::NormalExit ? result.exitCode : 1;
}

[[nodiscard]] int runAllowedCommand(const QString &command, const QStringList &commandArgs,
                                    bool interactive = false)
{
    const auto commandIt = allowedCommands().constFind(command);
    if (commandIt == allowedCommands().constEnd()) {
        printError(QString("Command is not allowed: %1").arg(command));
        return 127;
    }

    const QString resolvedCommand = resolveBinary(commandIt.value());
    if (resolvedCommand.isEmpty()) {
        printError(QString("Command is not available: %1").arg(command));
        return 127;
    }

    if (interactive) {
        return runProcessInteractive(resolvedCommand, commandArgs);
    }
    return relayResult(runProcess(resolvedCommand, commandArgs));
}

[[nodiscard]] int handleExec(const QStringList &args)
{
    if (args.isEmpty()) {
        printError(QStringLiteral("exec requires a command name"));
        return 1;
    }

    return runAllowedCommand(args.constFirst(), args.mid(1), true);
}

[[nodiscard]] int handleLockingProcess(const QStringList &args)
{
    if (args.size() != 1) {
        printError(QStringLiteral("locking-process requires exactly one path"));
        return 1;
    }

    const QString path = args.constFirst();
    if (!QFileInfo::exists(path)) {
        return 0;
    }

    const QString fuserBinary = resolveBinary(allowedCommands().value(QStringLiteral("fuser")));
    const QString psBinary = resolveBinary(allowedCommands().value(QStringLiteral("ps")));
    if (fuserBinary.isEmpty() || psBinary.isEmpty()) {
        printError(QStringLiteral("Required helper command is not available"));
        return 127;
    }

    const ProcessResult fuserResult = runProcess(fuserBinary, {path});
    const QString fuserOutput = QString::fromUtf8(fuserResult.standardOutput + fuserResult.standardError);
    const QRegularExpression pidRegex(QStringLiteral(R"((\d+))"));
    const QRegularExpressionMatch match = pidRegex.match(fuserOutput);
    if (!match.hasMatch()) {
        return 0;
    }

    const QString pid = match.captured(1);
    const ProcessResult psResult = runProcess(psBinary, {"--no-headers", "-o", "comm=", "-p", pid});
    if (!psResult.started || psResult.exitStatus != QProcess::NormalExit) {
        return 1;
    }

    writeAndFlush(stdout, psResult.standardOutput.trimmed());
    return 0;
}

[[nodiscard]] int handleRunHook(const QStringList &args)
{
    if (args.size() != 1) {
        printError(QStringLiteral("run-hook requires exactly one script"));
        return 1;
    }

    const QString script = args.constFirst().trimmed();
    if (script.isEmpty()) {
        return 0;
    }

    return relayResult(runProcess(QStringLiteral("/bin/bash"), {"-c", script}));
}
} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    const QString markerPath = qEnvironmentVariable("MX_PKG_HELPER_MARKER");
    if (!markerPath.isEmpty()) {
        QFile markerFile(markerPath);
        if (markerFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            markerFile.close();
        }
    }
    const QStringList arguments = app.arguments().mid(1);
    if (arguments.isEmpty()) {
        printError(QStringLiteral("Missing helper action"));
        return 1;
    }

    QStringList remainingArgs = arguments;
    const QString action = remainingArgs.takeFirst();

    if (action == QLatin1String("exec")) {
        return handleExec(remainingArgs);
    }
    if (action == QLatin1String("locking-process")) {
        return handleLockingProcess(remainingArgs);
    }
    if (action == QLatin1String("run-hook")) {
        return handleRunHook(remainingArgs);
    }

    printError(QString("Unsupported helper action: %1").arg(action));
    return 1;
}
