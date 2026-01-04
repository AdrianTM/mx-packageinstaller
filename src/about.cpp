/**********************************************************************
 *
 **********************************************************************
 * Copyright (C) 2023 MX Authors
 *
 * Authors: Adrian <adrian@mxlinux.org>
 *          MX Linux <http://mxlinux.org>
 *
 * This is free software: you can redistribute it and/or modify
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
#include "about.h"

#include <QApplication>
#include <QFileInfo>
#include <QMessageBox>
#include <QProcess>
#include <QPushButton>
#include <QStandardPaths>

#include <unistd.h>

extern const QString starting_home;

// Display doc as nomal user when run as root
void displayDoc(const QString &url, const QString &title)
{
    bool started_as_root = false;
    if (qEnvironmentVariable("HOME") == "root") {
        started_as_root = true;
        qputenv("HOME", starting_home.toUtf8()); // Use original home for theming purposes
    }
    // Prefer mx-viewer otherwise use xdg-open (use runuser to run that as logname user)
    QString executablePath = QStandardPaths::findExecutable("mx-viewer");
    if (!executablePath.isEmpty()) {
        QProcess::startDetached("mx-viewer", {url, title});
    } else {
        if (getuid() != 0) {
            QProcess::startDetached("xdg-open", {url});
        } else {
            QProcess proc;
            proc.start("logname", {}, QIODevice::ReadOnly);
            proc.waitForFinished();
            QString user = QString::fromUtf8(proc.readAllStandardOutput()).trimmed();
            QProcess::startDetached("runuser", {"-u", user, "--", "xdg-open", url});
        }
    }
    if (started_as_root) {
        qputenv("HOME", "/root");
    }
}

void displayAboutMsgBox(const QString &title, const QString &message, const QString &licence_url,
                        const QString &license_title)
{
    QMessageBox msgBox(QMessageBox::NoIcon, title, message);
    auto *btnLicense = msgBox.addButton(QObject::tr("License"), QMessageBox::HelpRole);
    auto *btnCancel = msgBox.addButton(QObject::tr("Cancel"), QMessageBox::NoRole);
    btnCancel->setIcon(QIcon::fromTheme("window-close"));

    msgBox.exec();

    if (msgBox.clickedButton() == btnLicense) {
        displayDoc(licence_url, license_title);
    }
}
