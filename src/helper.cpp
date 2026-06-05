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

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <vector>

#include <pty.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

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
        {"snap", {"/usr/bin/snap", "/snap/bin/snap"}},
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

[[nodiscard]] ProcessResult runProcess(const QString &program, const QStringList &args,
                                       const QProcessEnvironment &environment = QProcessEnvironment::systemEnvironment())
{
    ProcessResult result;

    QProcess process;
    process.setProcessEnvironment(environment);
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
[[nodiscard]] int runProcessInteractive(const QString &program, const QStringList &args,
                                        const QProcessEnvironment &environment = QProcessEnvironment::systemEnvironment())
{
    QProcess process;
    process.setProcessEnvironment(environment);
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

// Run a command attached to a pseudo-terminal so tools that gate their progress output
// on isatty() (notably `snap`) emit their live progress instead of staying silent until
// completion. Output is relayed to our stdout in real time; stdin is forwarded so the
// child can still read input. Returns the child's exit code.
[[nodiscard]] int runProcessOnPty(const QString &program, const QStringList &args,
                                  const QProcessEnvironment &environment)
{
    // Materialize argv and envp before forking; the child must avoid heap allocation
    // (and Qt calls) between fork() and exec().
    const QByteArray programBytes = program.toLocal8Bit();
    QList<QByteArray> argStorage;
    argStorage.reserve(args.size());
    for (const QString &arg : args) {
        argStorage.append(arg.toLocal8Bit());
    }
    std::vector<char *> argv;
    argv.reserve(static_cast<size_t>(args.size()) + 2);
    argv.push_back(const_cast<char *>(programBytes.constData()));
    for (QByteArray &arg : argStorage) {
        argv.push_back(arg.data());
    }
    argv.push_back(nullptr);

    QList<QByteArray> envStorage;
    const QStringList envKeys = environment.keys();
    envStorage.reserve(envKeys.size());
    for (const QString &key : envKeys) {
        envStorage.append((key + '=' + environment.value(key)).toLocal8Bit());
    }
    std::vector<char *> envp;
    envp.reserve(static_cast<size_t>(envStorage.size()) + 1);
    for (QByteArray &entry : envStorage) {
        envp.push_back(entry.data());
    }
    envp.push_back(nullptr);

    // Hand the child a roomy terminal so progress bars are not truncated.
    struct winsize ws {};
    ws.ws_row = 24;
    ws.ws_col = 120;

    int masterFd = -1;
    const pid_t pid = forkpty(&masterFd, nullptr, nullptr, &ws);
    if (pid < 0) {
        printError(QStringLiteral("Failed to allocate pseudo-terminal"));
        return 127;
    }

    if (pid == 0) {
        // forkpty leaves the pty master open in the child; close it (and any other
        // inherited descriptors above stdio) so the privileged child only keeps the
        // terminal it needs. Best effort: ignored on kernels without close_range.
        close_range(STDERR_FILENO + 1, ~0U, 0);
        execve(programBytes.constData(), argv.data(), envp.data());
        _exit(127); // exec failed
    }

    const int stdinFd = fileno(stdin);
    bool stdinOpen = stdinFd >= 0;
    char buffer[4096];

    for (;;) {
        fd_set readFds;
        FD_ZERO(&readFds);
        FD_SET(masterFd, &readFds);
        int maxFd = masterFd;
        if (stdinOpen) {
            FD_SET(stdinFd, &readFds);
            maxFd = std::max(maxFd, stdinFd);
        }

        if (select(maxFd + 1, &readFds, nullptr, nullptr, nullptr) < 0) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }

        if (FD_ISSET(masterFd, &readFds)) {
            const ssize_t count = read(masterFd, buffer, sizeof(buffer));
            if (count > 0) {
                writeAndFlush(stdout, QByteArray::fromRawData(buffer, static_cast<int>(count)));
            } else if (count == 0) {
                break;
            } else if (errno != EINTR && errno != EAGAIN) {
                break; // EIO on Linux once the child has closed the slave side
            }
        }

        if (stdinOpen && FD_ISSET(stdinFd, &readFds)) {
            const ssize_t count = read(stdinFd, buffer, sizeof(buffer));
            if (count > 0) {
                ssize_t written = 0;
                while (written < count) {
                    const ssize_t bytes = write(masterFd, buffer + written, static_cast<size_t>(count - written));
                    if (bytes <= 0) {
                        break;
                    }
                    written += bytes;
                }
            } else if (count == 0 || (errno != EINTR && errno != EAGAIN)) {
                stdinOpen = false; // our stdin closed; stop forwarding
            }
        }
    }

    int status = 0;
    while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {
    }
    close(masterFd);

    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    }
    return 1;
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

    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    // The snap client warns "/snap/bin is not in your $PATH" based on the PATH of
    // the process that invokes it. Running through this elevated helper, that PATH
    // is root's sanitized one, which never contains /snap/bin, so the warning shows
    // on every install regardless of the user's session (and a reboot won't help).
    // Ensure the snap process sees /snap/bin so the spurious warning is suppressed.
    if (command == QLatin1String("snap")) {
        QString path = environment.value(QStringLiteral("PATH"));
        if (!path.split(QLatin1Char(':')).contains(QLatin1String("/snap/bin"))) {
            path = path.isEmpty() ? QStringLiteral("/snap/bin") : path + QStringLiteral(":/snap/bin");
            environment.insert(QStringLiteral("PATH"), path);
        }
        // snap stays silent unless it believes it is writing to a terminal, so run it
        // under a pseudo-terminal to surface its live progress in the Output tab.
        return runProcessOnPty(resolvedCommand, commandArgs, environment);
    }

    if (interactive) {
        return runProcessInteractive(resolvedCommand, commandArgs, environment);
    }
    return relayResult(runProcess(resolvedCommand, commandArgs, environment));
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
