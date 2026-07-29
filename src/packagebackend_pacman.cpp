/**********************************************************************
 *  packagebackend_pacman.cpp
 **********************************************************************
 * Copyright (C) 2026-2026 MX Authors
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

#include "packagebackend.h"

#include <QProcess>

#include "cmd.h"

bool PackageBackend::refreshRepositories(Cmd &cmd)
{
    return cmd.procScriptAsRoot(QStringLiteral("/usr/lib/mx-packageinstaller/mxpi-maintenance"),
                                {QStringLiteral("repo_sync")}, nullptr, nullptr, Cmd::QuietMode::Yes);
}

QHash<QString, PackageInfo> PackageBackend::listInstalled(bool *ok)
{
    Cmd shell;
    const QString list = shell.getOut("LANG=C pacman -Qi");

    if (ok) {
        *ok = shell.exitStatus() == QProcess::NormalExit && shell.exitCode() == 0;
    }
    if (shell.exitStatus() != QProcess::NormalExit || shell.exitCode() != 0) {
        return {};
    }

    QHash<QString, PackageInfo> installedPackagesMap;
    QString packageName;
    QString version;
    QString description;

    auto flushPackage = [&]() {
        if (!packageName.isEmpty()) {
            installedPackagesMap.insert(packageName, {version, description});
        }
        packageName.clear();
        version.clear();
        description.clear();
    };

    const auto lines = list.split('\n');
    for (const QString &line : lines) {
        if (line.startsWith("Name")) {
            flushPackage();
            packageName = line.section(':', 1).trimmed();
        } else if (line.startsWith("Version")) {
            version = line.section(':', 1).trimmed();
        } else if (line.startsWith("Description")) {
            description = line.section(':', 1).trimmed();
        }
    }
    flushPackage();

    return installedPackagesMap;
}

QHash<QString, VersionNumber> PackageBackend::listInstalledVersions(bool *ok)
{
    QHash<QString, VersionNumber> installedVersions;
    Cmd shell;
    const QString list = shell.getOut("LANG=C pacman -Q", Cmd::QuietMode::Yes);

    if (ok) {
        *ok = shell.exitStatus() == QProcess::NormalExit && shell.exitCode() == 0;
    }
    if (shell.exitStatus() != QProcess::NormalExit || shell.exitCode() != 0) {
        return installedVersions;
    }

    for (const QString &line : list.split('\n', Qt::SkipEmptyParts)) {
        const QStringList parts = line.split(' ', Qt::SkipEmptyParts);
        if (parts.size() == 2) {
            installedVersions.insert(parts.at(0), VersionNumber(parts.at(1)));
        }
    }
    return installedVersions;
}

bool PackageBackend::markManuallyInstalled(Cmd &cmd, const QStringList &names)
{
    QStringList args {"-D", "--asexplicit"};
    args += names;
    return cmd.procAsRoot("pacman", args);
}

QStringList PackageBackend::autoremovableCandidates(Cmd &cmd)
{
    return cmd.getOut("LANG=C pacman -Qtdq").split('\n', Qt::SkipEmptyParts);
}
