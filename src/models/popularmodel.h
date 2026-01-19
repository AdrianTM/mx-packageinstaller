/**********************************************************************
 * Copyright (C) 2017-2025 MX Authors
 *
 * Authors: Adrian
 *          MX Linux <http://mxlinux.org>
 *
 * This file is part of mx-packageinstaller.
 *
 * mx-packageinstaller is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * mx-packageinstaller is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with mx-packageinstaller.  If not, see <http://www.gnu.org/licenses/>.
 **********************************************************************/
#pragma once

#include <QHash>
#include <QIcon>
#include <QStandardItemModel>

namespace PopCol
{
enum {
    Icon,
    Check,
    Name,
    Info,
    Description,
    InstallNames,
    UninstallNames,
    Screenshot,
    PostUninstall,
    PreUninstall,
    MAX
};
}

struct PopularAppData {
    QString category;
    QString name;
    QString description;
    QString installNames;
    QString uninstallNames;
    QString screenshot;
    QString postUninstall;
    QString preUninstall;
    bool isInstalled = false;
};

class PopularModel : public QStandardItemModel
{
    Q_OBJECT

public:
    explicit PopularModel(QObject *parent = nullptr);

    void setPopularApps(const QList<PopularAppData> &apps);
    void clear();

    void setInstalledPackages(const std::function<bool(const QString &)> &checkInstalled);

    [[nodiscard]] QStringList checkedPackageNames() const;
    [[nodiscard]] QList<QStandardItem *> checkedItems() const;
    void uncheckAll();

    [[nodiscard]] QStandardItem *findItemByName(const QString &name) const;

    void setIcons(const QIcon &installed, const QIcon &folder, const QIcon &info);

signals:
    void itemCheckStateChanged(QStandardItem *item);

private:
    [[nodiscard]] QStandardItem *getOrCreateCategory(const QString &name);

    QHash<QString, QStandardItem *> m_categoryItems;
    QIcon m_iconInstalled;
    QIcon m_iconFolder;
    QIcon m_iconInfo;
};
