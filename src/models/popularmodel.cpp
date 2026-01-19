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
#include "popularmodel.h"

PopularModel::PopularModel(QObject *parent)
    : QStandardItemModel(parent)
{
    setColumnCount(PopCol::MAX);

    // Set column headers
    setHorizontalHeaderLabels({
        tr("Category"),      // Icon
        tr(""),              // Check
        tr("Package"),       // Name
        tr("Info"),          // Info
        tr("Description"),   // Description
        tr(""),              // InstallNames (hidden)
        tr(""),              // UninstallNames (hidden)
        tr(""),              // Screenshot (hidden)
        tr(""),              // PostUninstall (hidden)
        tr("")               // PreUninstall (hidden)
    });
}

void PopularModel::setPopularApps(const QList<PopularAppData> &apps)
{
    // Don't call beginResetModel here - QStandardItemModel::clear() does it internally!
    QStandardItemModel::clear();
    m_categoryItems.clear();

    // Re-set column count and headers after clear()
    setColumnCount(PopCol::MAX);
    setHorizontalHeaderLabels({
        tr("Category"),      // Icon
        tr(""),              // Check
        tr("Package"),       // Name
        tr("Info"),          // Info
        tr("Description"),   // Description
        tr(""),              // InstallNames (hidden)
        tr(""),              // UninstallNames (hidden)
        tr(""),              // Screenshot (hidden)
        tr(""),              // PostUninstall (hidden)
        tr("")               // PreUninstall (hidden)
    });

    QFont boldFont;
    boldFont.setBold(true);

    for (const PopularAppData &app : apps) {
        QStandardItem *categoryItem = getOrCreateCategory(app.category);

        auto *iconItem = new QStandardItem();
        iconItem->setFlags(iconItem->flags() & ~Qt::ItemIsEditable);

        auto *checkItem = new QStandardItem();
        checkItem->setCheckable(true);
        checkItem->setCheckState(Qt::Unchecked);
        checkItem->setFlags(checkItem->flags() & ~Qt::ItemIsEditable);
        // Put the installed icon in the checkbox column
        if (app.isInstalled) {
            checkItem->setIcon(m_iconInstalled);
        }

        auto *nameItem = new QStandardItem(app.name);
        nameItem->setFlags(nameItem->flags() & ~Qt::ItemIsEditable);

        auto *infoItem = new QStandardItem();
        infoItem->setIcon(m_iconInfo);
        infoItem->setFlags(infoItem->flags() & ~Qt::ItemIsEditable);

        auto *descItem = new QStandardItem(app.description);
        descItem->setFlags(descItem->flags() & ~Qt::ItemIsEditable);

        auto *installNamesItem = new QStandardItem(app.installNames);
        installNamesItem->setFlags(installNamesItem->flags() & ~Qt::ItemIsEditable);

        auto *uninstallNamesItem = new QStandardItem();
        uninstallNamesItem->setData(app.uninstallNames, Qt::UserRole);
        uninstallNamesItem->setFlags(uninstallNamesItem->flags() & ~Qt::ItemIsEditable);

        auto *screenshotItem = new QStandardItem();
        screenshotItem->setData(app.screenshot, Qt::UserRole);
        screenshotItem->setFlags(screenshotItem->flags() & ~Qt::ItemIsEditable);

        auto *postUninstallItem = new QStandardItem();
        postUninstallItem->setData(app.postUninstall, Qt::UserRole);
        postUninstallItem->setFlags(postUninstallItem->flags() & ~Qt::ItemIsEditable);

        auto *preUninstallItem = new QStandardItem();
        preUninstallItem->setData(app.preUninstall, Qt::UserRole);
        preUninstallItem->setFlags(preUninstallItem->flags() & ~Qt::ItemIsEditable);

        QList<QStandardItem *> row;
        row.reserve(PopCol::MAX);
        row << iconItem << checkItem << nameItem << infoItem << descItem << installNamesItem << uninstallNamesItem
            << screenshotItem << postUninstallItem << preUninstallItem;

        categoryItem->appendRow(row);
    }

    // No endResetModel needed - clear() already handled the reset
    // Don't sort here - it causes nested begin/endResetModel calls that lose data
    // Instead, sort in displayPopularApps after the model is fully set up
}

void PopularModel::clear()
{
    beginResetModel();
    QStandardItemModel::clear();
    m_categoryItems.clear();
    endResetModel();
}

void PopularModel::setInstalledPackages(const std::function<bool(const QString &)> &checkInstalled)
{
    for (int catRow = 0; catRow < rowCount(); ++catRow) {
        QStandardItem *categoryItem = item(catRow);
        if (!categoryItem) {
            continue;
        }

        for (int row = 0; row < categoryItem->rowCount(); ++row) {
            QStandardItem *uninstallItem = categoryItem->child(row, PopCol::UninstallNames);
            QStandardItem *checkItem = categoryItem->child(row, PopCol::Check);
            if (uninstallItem && checkItem) {
                QString uninstallNames = uninstallItem->data(Qt::UserRole).toString();
                if (checkInstalled(uninstallNames)) {
                    checkItem->setIcon(m_iconInstalled);
                } else {
                    checkItem->setIcon(QIcon());
                }
            }
        }
    }
}

QStringList PopularModel::checkedPackageNames() const
{
    QStringList result;
    for (int catRow = 0; catRow < rowCount(); ++catRow) {
        QStandardItem *categoryItem = item(catRow);
        if (!categoryItem) {
            continue;
        }

        for (int row = 0; row < categoryItem->rowCount(); ++row) {
            QStandardItem *checkItem = categoryItem->child(row, PopCol::Check);
            QStandardItem *nameItem = categoryItem->child(row, PopCol::Name);
            if (checkItem && nameItem && checkItem->checkState() == Qt::Checked) {
                result.append(nameItem->text());
            }
        }
    }
    return result;
}

QList<QStandardItem *> PopularModel::checkedItems() const
{
    QList<QStandardItem *> result;
    for (int catRow = 0; catRow < rowCount(); ++catRow) {
        QStandardItem *categoryItem = item(catRow);
        if (!categoryItem) {
            continue;
        }

        for (int row = 0; row < categoryItem->rowCount(); ++row) {
            QStandardItem *checkItem = categoryItem->child(row, PopCol::Check);
            if (checkItem && checkItem->checkState() == Qt::Checked) {
                result.append(checkItem);
            }
        }
    }
    return result;
}

void PopularModel::uncheckAll()
{
    for (int catRow = 0; catRow < rowCount(); ++catRow) {
        QStandardItem *categoryItem = item(catRow);
        if (!categoryItem) {
            continue;
        }

        for (int row = 0; row < categoryItem->rowCount(); ++row) {
            QStandardItem *checkItem = categoryItem->child(row, PopCol::Check);
            if (checkItem && checkItem->checkState() == Qt::Checked) {
                checkItem->setCheckState(Qt::Unchecked);
            }
        }
    }
}

QStandardItem *PopularModel::findItemByName(const QString &name) const
{
    for (int catRow = 0; catRow < rowCount(); ++catRow) {
        QStandardItem *categoryItem = item(catRow);
        if (!categoryItem) {
            continue;
        }

        for (int row = 0; row < categoryItem->rowCount(); ++row) {
            QStandardItem *nameItem = categoryItem->child(row, PopCol::Name);
            if (nameItem && nameItem->text() == name) {
                return categoryItem->child(row, PopCol::Check);
            }
        }
    }
    return nullptr;
}

void PopularModel::setIcons(const QIcon &installed, const QIcon &folder, const QIcon &info)
{
    m_iconInstalled = installed;
    m_iconFolder = folder;
    m_iconInfo = info;
}

QStandardItem *PopularModel::getOrCreateCategory(const QString &name)
{
    auto it = m_categoryItems.find(name);
    if (it != m_categoryItems.end()) {
        return it.value();
    }

    QFont boldFont;
    boldFont.setBold(true);

    // Create the category item for the first column (icon/name)
    auto *categoryItem = new QStandardItem(name);
    categoryItem->setFont(boldFont);
    categoryItem->setIcon(m_iconFolder);
    categoryItem->setFlags(categoryItem->flags() & ~Qt::ItemIsSelectable);

    // Create a full row for the category with all columns
    QList<QStandardItem *> categoryRow;
    categoryRow.reserve(PopCol::MAX);
    categoryRow << categoryItem; // First column (icon)

    // Add empty items for the remaining columns
    for (int i = 1; i < PopCol::MAX; ++i) {
        auto *emptyItem = new QStandardItem();
        emptyItem->setFlags(emptyItem->flags() & ~Qt::ItemIsSelectable);
        categoryRow << emptyItem;
    }

    appendRow(categoryRow);
    m_categoryItems.insert(name, categoryItem);

    return categoryItem;
}
