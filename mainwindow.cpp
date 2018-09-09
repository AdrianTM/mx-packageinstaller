/**********************************************************************
 *  MainWindow.cpp
 **********************************************************************
 * Copyright (C) 2017 MX Authors
 *
 * Authors: Adrian
 *          Dolphin_Oracle
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

#include <QFileDialog>
#include <QImageReader>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QProgressBar>
#include <QScrollBar>
#include <QTextStream>
#include <QtXml/QtXml>

#include <QDebug>

#include "mainwindow.h"
#include "ui_mainwindow.h"

#include "aptcache.h"
#include "versionnumber.h"



MainWindow::MainWindow(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::MainWindow),
    dictionary("/usr/share/mx-packageinstaller-pkglist/category.dict", QSettings::IniFormat)
{
    ui->setupUi(this);
    setup();
}

MainWindow::~MainWindow()
{
    delete ui;
}

// Setup versious items first time program runs
void MainWindow::setup()
{
    qDebug() << "+++ Enter Function:" << __PRETTY_FUNCTION__ << "+++";
    ui->tabWidget->blockSignals(true);
    cmd = new Cmd(this);
    user = "--system ";

    if (system("arch | grep -q x86_64") == 0) {
        arch = "amd64";
    } else {
        arch = "i386";
    }
    QString ver_num = getDebianVersion();
    if (ver_num == "8") {
        ver_name = "jessie";
    } else if (ver_num == "9") {
        ver_name = "stretch";
    }
    setProgressDialog();
    lock_file = new LockFile("/var/lib/dpkg/lock");
    connect(qApp, &QApplication::aboutToQuit, this, &MainWindow::cleanup);
    version = getVersion("mx-packageinstaller");
    this->setWindowTitle(tr("MX Package Installer"));
    ui->tabWidget->setCurrentIndex(0);
    QStringList column_names;
    column_names << "" << "" << tr("Package") << tr("Info") << tr("Description");
    ui->treePopularApps->setHeaderLabels(column_names);
    ui->treeStable->hideColumn(5); // Status of the package: installed, upgradable, etc
    ui->treeStable->hideColumn(6); // Displayed status true/false
    ui->treeMXtest->hideColumn(5); // Status of the package: installed, upgradable, etc
    ui->treeMXtest->hideColumn(6); // Displayed status true/false
    ui->treeBackports->hideColumn(5); // Status of the package: installed, upgradable, etc
    ui->treeBackports->hideColumn(6); // Displayed status true/false
    ui->treeFlatpak->hideColumn(5); // Status
    ui->treeFlatpak->hideColumn(6); // Displayed
    ui->treeFlatpak->hideColumn(7); // Duplication
    ui->treeFlatpak->hideColumn(8); // Full string
    ui->icon->setIcon(QIcon::fromTheme("software-update-available", QIcon(":/icons/software-update-available.png")));
    ui->icon_2->setIcon(QIcon::fromTheme("software-update-available", QIcon(":/icons/software-update-available.png")));
    ui->icon_3->setIcon(QIcon::fromTheme("software-update-available", QIcon(":/icons/software-update-available.png")));
    loadPmFiles();
    refreshPopularApps();

    // connect search boxes
    connect(ui->searchPopular, &QLineEdit::textChanged, this, &MainWindow::findPopular);
    connect(ui->searchBoxStable, &QLineEdit::textChanged, this, &MainWindow::findPackageOther);
    connect(ui->searchBoxMX, &QLineEdit::textChanged, this, &MainWindow::findPackageOther);
    connect(ui->searchBoxBP, &QLineEdit::textChanged, this, &MainWindow::findPackageOther);
    connect(ui->searchBoxFlatpak, &QLineEdit::textChanged, this, &MainWindow::findPackageOther);

    // connect combo filters
    connect(ui->comboFilterStable, &QComboBox::currentTextChanged, this, &MainWindow::filterChanged);
    connect(ui->comboFilterMX, &QComboBox::currentTextChanged, this, &MainWindow::filterChanged);
    connect(ui->comboFilterBP, &QComboBox::currentTextChanged, this, &MainWindow::filterChanged);
    connect(ui->comboFilterFlatpak, &QComboBox::currentTextChanged, this, &MainWindow::filterChanged);

    ui->searchPopular->setFocus();
    updated_once = false;
    warning_displayed = false;
    tree = ui->treePopularApps;
    ui->tabWidget->setTabEnabled(ui->tabWidget->indexOf(ui->tabOutput), false);
    ui->tabWidget->blockSignals(false);
}

// Uninstall listed packages
bool MainWindow::uninstall(const QString &names)
{
    qDebug() << "+++ Enter Function:" << __PRETTY_FUNCTION__ << "+++";
    ui->tabWidget->setCurrentWidget(ui->tabOutput);

    lock_file->unlock();
    ui->tabWidget->setTabText(ui->tabWidget->indexOf(ui->tabOutput), tr("Uninstalling packages..."));
    setConnections();
    cmd->run("DEBIAN_FRONTEND=gnome apt-get remove " + names + "| tee -a /var/log/mxpi.log");
    lock_file->lock();

    return (cmd->getExitCode(true) == 0);
}

// Run apt-get update
bool MainWindow::update()
{
    qDebug() << "+++ Enter Function:" << __PRETTY_FUNCTION__ << "+++";
    QString msg;
    lock_file->unlock();
    if (!ui->tabOutput->isVisible()) { // don't display in output if calling to refresh from tabs
        progress->show();
    } else {
        ui->tabWidget->setTabText(ui->tabWidget->indexOf(ui->tabOutput), tr("Refreshing sources..."));
    }
    setConnections();
    if (cmd->run("apt-get update -o Acquire::http:Timeout=10 -o Acquire::https:Timeout=10 -o Acquire::ftp:Timeout=10 | tee -a /var/log/mxpi.log") == 0) {
        lock_file->lock();
        msg="echo sources updated OK >>/var/log/mxpi.log";
        system(msg.toUtf8());
        updated_once = true;
        return true;
    }
    lock_file->lock();
    msg="echo problem updating sources >>/var/log/mxpi.log";
    system(msg.toUtf8());
    QMessageBox::critical(this, tr("Error"), tr("There was a problem updating sources. Some sources may not have provided updates. For more info check: ") +
                          "<a href=\"/var/log/mxpi.log\">/var/log/mxpi.log</a>");
    return false;
}


// Add sizes for the installed packages for older flatpak that doesn't list size for all the packages
void MainWindow::addInstalledSizesFP() const
{
    qDebug() << "+++ Enter Function:" << __PRETTY_FUNCTION__ << "+++";
    // list installed packages with sizes
    QStringList list = cmd->getOutput("su $(logname) -c \"flatpak -d list --app " + user + "|tr -s ' ' |cut -f1,5,6 -d' '\"").split("\n");
    QStringList runtimes = cmd->getOutput("su $(logname) -c \"flatpak -d list --runtime " + user + "|tr -s ' ' |cut -f1,5,6 -d' '\"").split("\n");
    if (!runtimes.isEmpty()) {
        list << runtimes;
    }

    QString name, size;
    QTreeWidgetItemIterator it(ui->treeFlatpak);
    while (*it) {
        foreach (QString item, list) {
            name = item.section(" ", 0, 0);
            size = item.section(" ", 1);
            if (name == (*it)->text(8)) {
                (*it)->setText(4, size);
            }
        }
        ++it;
    }
}

// Block interface while updateing Flatpak list
void MainWindow::blockInterfaceFP(bool block)
{
    for (int tab = 0; tab < 4; ++tab) {
        ui->tabWidget->setTabEnabled(tab, !block);
    }
    ui->comboRemote->setDisabled(block);
    ui->comboFilterFlatpak->setDisabled(block);
    ui->comboUser->setDisabled(block);
    ui->searchBoxFlatpak->setDisabled(block);
    ui->treeFlatpak->setDisabled(block);
    ui->frameFP->setDisabled(block);
    ui->labelFP->setDisabled(block);
    ui->labelRepo->setDisabled(block);
    if (block) {
        setCursor(QCursor(Qt::BusyCursor));
    } else {
        setCursor(QCursor(Qt::ArrowCursor));
    }
}

// Update interface when done loading info
void MainWindow::updateInterface()
{
    qDebug() << "+++ Enter Function:" << __PRETTY_FUNCTION__ << "+++";

    QList<QTreeWidgetItem *> upgr_list = tree->findItems("upgradable", Qt::MatchExactly, 5);
    QList<QTreeWidgetItem *> inst_list = tree->findItems("installed", Qt::MatchExactly, 5);

    if (tree == ui->treeStable) {
        ui->labelNumApps->setText(QString::number(tree->topLevelItemCount()));
        ui->labelNumUpgr->setText(QString::number(upgr_list.count()));
        ui->labelNumInst->setText(QString::number(inst_list.count() + upgr_list.count()));
        ui->buttonUpgradeAll->setVisible(upgr_list.count() > 0);
        ui->buttonForceUpdateStable->setEnabled(true);
        ui->searchBoxStable->setFocus();
    } else if (tree == ui->treeMXtest) {
        ui->labelNumApps_2->setText(QString::number(tree->topLevelItemCount()));
        ui->labelNumUpgrMX->setText(QString::number(upgr_list.count()));
        ui->labelNumInstMX->setText(QString::number(inst_list.count() + upgr_list.count()));
        ui->buttonForceUpdateMX->setEnabled(true);
        ui->searchBoxMX->setFocus();
    } else if (tree == ui->treeBackports) {
        ui->labelNumApps_3->setText(QString::number(tree->topLevelItemCount()));
        ui->labelNumUpgrBP->setText(QString::number(upgr_list.count()));
        ui->labelNumInstBP->setText(QString::number(inst_list.count() + upgr_list.count()));
        ui->buttonForceUpdateBP->setEnabled(true);
        ui->searchBoxBP->setFocus();
    }

    QApplication::setOverrideCursor(QCursor(Qt::ArrowCursor));
    progress->hide();
}

// Returns Debian main version number
QString MainWindow::getDebianVersion() const
{
    return cmd->getOutput("cat /etc/debian_version | cut -f1 -d'.'", QStringList() << "quiet");
}

// Returns localized name for elements
QString MainWindow::getLocalizedName(const QDomElement element) const
{
    // pass one, find fully localized string, e.g. "pt_BR"
    QDomElement child = element.firstChildElement();
    for(; (!child.isNull()); child = child.nextSiblingElement() ) {
        if (child.tagName() == locale.name() && !child.text().trimmed().isEmpty()) {
            return child.text().trimmed();
        }
    }
    // pass two, find language, e.g. "pt"
    child = element.firstChildElement();
    for(; (!child.isNull()); child = child.nextSiblingElement()) {
        if (child.tagName() == locale.name().section("_", 0, 0) && !child.text().trimmed().isEmpty()) {
            return child.text().trimmed();
        }
    }
    // pass three, return "en" or "en_US"
    child = element.firstChildElement();
    for(; (!child.isNull()); child = child.nextSiblingElement()) {
        if ((child.tagName() == "en" || child.tagName() == "en_US") && !child.text().trimmed().isEmpty()) {
            return child.text().trimmed();
        }
    }

    child = element.firstChildElement();
    if (child.isNull()) {
        return element.text().trimmed(); // if no language tags are present
    } else {
        return child.text().trimmed(); // return first language tag if neither the specified locale nor "en" is found.
    }
}

// get translation for the category
QString MainWindow::getTranslation(const QString item)
{
    if (locale.name() == "en_US" ) { // no need for translation
        return item;
    }

    dictionary.beginGroup(item);

    QString trans = dictionary.value(locale.name()).toString().toLatin1(); // try pt_BR format
    if (trans.isEmpty()) {
        trans = dictionary.value(locale.name().section("_", 0, 0)).toString().toLatin1(); // try pt format
        if (trans.isEmpty()) {
            dictionary.endGroup();
            return item;  // return original item if no translation found
        }
    }
    dictionary.endGroup();
    return trans;
}

// Set proc and timer connections
void MainWindow::setConnections() const
{
    qDebug() << "+++ Enter Function:" << __PRETTY_FUNCTION__ << "+++";
    connect(cmd, &Cmd::runTime, this, &MainWindow::updateBar, Qt::UniqueConnection);  // processes runtime emited by Cmd to be used by a progress bar
    connect(cmd, &Cmd::outputAvailable, this, &MainWindow::updateOutput, Qt::UniqueConnection);
    connect(cmd, &Cmd::errorAvailable, this, &MainWindow::updateOutput, Qt::UniqueConnection);
    connect(cmd, &Cmd::started, this, &MainWindow::cmdStart, Qt::UniqueConnection);
    connect(cmd, &Cmd::finished, this, &MainWindow::cmdDone, Qt::UniqueConnection);
}

// Processes tick emited by Cmd to be used by a progress bar
void MainWindow::updateBar(int counter, int duration)
{
    int max_value = (duration != 0) ? duration : 10;
    bar->setMaximum(max_value);
    bar->setValue(counter % (max_value + 1));
}

void MainWindow::updateOutput(const QString out) const
{
    ui->outputBox->insertPlainText(out);

    QScrollBar *sb = ui->outputBox->verticalScrollBar();
    sb->setValue(sb->maximum());
}


// Load info from the .pm files
void MainWindow::loadPmFiles()
{
    qDebug() << "+++ Enter Function:" << __PRETTY_FUNCTION__ << "+++";
    QDomDocument doc;

    QStringList filter("*.pm");
    QDir dir("/usr/share/mx-packageinstaller-pkglist");
    QStringList pmfilelist = dir.entryList(filter);

    foreach (const QString &file_name, pmfilelist) {
        QFile file(dir.absolutePath() + "/" + file_name);
        if (!file.open(QFile::ReadOnly | QFile::Text)) {
            qDebug() << "Could not open: " << file.fileName();
        } else {
            if (!doc.setContent(&file)) {
                qDebug() << "Could not load document: " << file_name << "-- not valid XML?";
            } else {
                processDoc(doc);
            }
        }
        file.close();
    }
}

// Process dom documents (from .pm files)
void MainWindow::processDoc(const QDomDocument &doc)
{
    /*  Order items in list:
        0 "category"
        1 "name"
        2 "description"
        3 "installable"
        4 "screenshot"
        5 "preinstall"
        6 "install_package_names"
        7 "postinstall"
        8 "uninstall_package_names"
    */

    QString category;
    QString name;
    QString description;
    QString installable;
    QString screenshot;
    QString preinstall;
    QString postinstall;
    QString install_names;
    QString uninstall_names;
    QStringList list;

    QDomElement root = doc.firstChildElement("app");
    QDomElement element = root.firstChildElement();

    for (; !element.isNull(); element = element.nextSiblingElement()) {
        if (element.tagName() == "category") {
            category = getTranslation(element.text().trimmed());
        } else if (element.tagName() == "name") {
            name = element.text().trimmed();
        } else if (element.tagName() == "description") {
            description = getLocalizedName(element);
        } else if (element.tagName() == "installable") {
            installable = element.text().trimmed();
        } else if (element.tagName() == "screenshot") {
            screenshot = element.text().trimmed();
        } else if (element.tagName() == "preinstall") {
            preinstall = element.text().trimmed();
        } else if (element.tagName() == "install_package_names") {
            install_names = element.text().trimmed();
            install_names.replace("\n", " ");
        } else if (element.tagName() == "postinstall") {
            postinstall = element.text().trimmed();
        } else if (element.tagName() == "uninstall_package_names") {
            uninstall_names = element.text().trimmed();
        }
    }
    // skip non-installable packages
    if ((installable == "64" && arch != "amd64") || (installable == "32" && arch != "i386")) {
        return;
    }
    list << category << name << description << installable << screenshot << preinstall
         << postinstall << install_names << uninstall_names;
    popular_apps << list;
}

// Reload and refresh interface
void MainWindow::refreshPopularApps()
{
    qDebug() << "+++ Enter Function:" << __PRETTY_FUNCTION__ << "+++";
    ui->treePopularApps->clear();
    ui->searchPopular->clear();
    ui->buttonInstall->setEnabled(false);
    ui->buttonUninstall->setEnabled(false);
    installed_packages = listInstalled();
    displayPopularApps();
}

// In case of duplicates add extra name to disambiguate
void MainWindow::removeDuplicatesFP()
{
    // find and mark duplicates
    QTreeWidgetItemIterator it(ui->treeFlatpak);
    QString current, next;
    while (*it) {
        current = ((*it))->text(1);\
        if (*(++it)) {
            next = ((*it))->text(1);
            if (next == current) {
                --it;
                (*(it))->setText(7, "Duplicate");
                ++it;
                (*it)->setText(7, "Duplicate");
            }
        }
    }
    // rename duplicate to use more context
    QTreeWidgetItemIterator it2(ui->treeFlatpak);
    while (*it2) {
        if ((*(it2))->text(7) == "Duplicate") {
            (*it2)->setText(1, (*it2)->text(2).section(".", -2));
        }
        ++it2;
    }
}

// Setup progress dialog
void MainWindow::setProgressDialog()
{
    timer = new QTimer(this);
    progress = new QProgressDialog(this);
    bar = new QProgressBar(progress);
    progCancel = new QPushButton(tr("Cancel"));
    connect(progCancel, &QPushButton::clicked, this, &MainWindow::cancelDownload);
    progress->setWindowModality(Qt::WindowModal);
    progress->setWindowFlags(Qt::Dialog | Qt::CustomizeWindowHint | Qt::WindowTitleHint |Qt::WindowSystemMenuHint | Qt::WindowStaysOnTopHint);
    progress->setCancelButton(progCancel);
    progCancel->setDisabled(true);
    progress->setLabelText(tr("Please wait..."));
    progress->setAutoClose(false);
    progress->setBar(bar);
    bar->setTextVisible(false);
    progress->reset();
}

void MainWindow::setSearchFocus()
{
    if (ui->tabStable->isVisible()) {
        ui->searchBoxStable->setFocus();
    } else if (ui->tabMXtest->isVisible()) {
        ui->searchBoxMX->setFocus();
    } else if (ui->tabBackports->isVisible()) {
        ui->searchBoxBP->setFocus();
    } else if (ui->tabFlatpak->isVisible()) {
        ui->searchBoxFlatpak->setFocus();
    }
}

// Display Popular Apps in the treePopularApps
void MainWindow::displayPopularApps() const
{
    qDebug() << "+++ Enter Function:" << __PRETTY_FUNCTION__ << "+++";
    QTreeWidgetItem *topLevelItem = 0L;
    QTreeWidgetItem *childItem;

    foreach (const QStringList &list, popular_apps) {
        QString category = list.at(0);
        QString name = list.at(1);
        QString description = list.at(2);
        QString installable = list.at(3);
        QString screenshot = list.at(4);
        QString preinstall = list.at(5);
        QString postinstall = list.at(6);
        QString install_names = list.at(7);
        QString uninstall_names = list.at(8);

        // add package category if treePopularApps doesn't already have it
        if (ui->treePopularApps->findItems(category, Qt::MatchFixedString, 2).isEmpty()) {
            topLevelItem = new QTreeWidgetItem();
            topLevelItem->setText(2, category);
            ui->treePopularApps->addTopLevelItem(topLevelItem);
            // topLevelItem look
            QFont font;
            font.setBold(true);
            topLevelItem->setFont(2, font);
            topLevelItem->setIcon(0, QIcon::fromTheme("folder"));
        } else {
            topLevelItem = ui->treePopularApps->findItems(category, Qt::MatchFixedString, 2).at(0); //find first match; add the child there
        }
        // add package name as childItem to treePopularApps
        childItem = new QTreeWidgetItem(topLevelItem);
        childItem->setText(2, name);
        childItem->setIcon(3, QIcon::fromTheme("dialog-information"));

        // add checkboxes
        childItem->setFlags(childItem->flags() | Qt::ItemIsUserCheckable);
        childItem->setCheckState(1, Qt::Unchecked);

        // add description from file
        childItem->setText(4, description);

        // add install_names (not displayed)
        childItem->setText(5, install_names);

        // add uninstall_names (not displayed)
        childItem->setText(6, uninstall_names);

        // add screenshot url (not displayed)
        childItem->setText(7, screenshot);

        // gray out installed items
        if (checkInstalled(uninstall_names)) {
            childItem->setForeground(2, QBrush(Qt::gray));
            childItem->setForeground(4, QBrush(Qt::gray));
        }
    }
    for (int i = 0; i < 5; ++i) {
        ui->treePopularApps->resizeColumnToContents(i);
    }
    ui->treePopularApps->sortItems(2, Qt::AscendingOrder);
    connect(ui->treePopularApps, &QTreeWidget::itemClicked, this, &MainWindow::displayInfo, Qt::UniqueConnection);
}


// Display only the listed apps
void MainWindow::displayFiltered(const QStringList &list) const
{
    qDebug() << "+++ Enter Function:" << __PRETTY_FUNCTION__ << "+++";

    QStringList new_list;
    foreach (QString item, list) {
        new_list << item.section("\t", 0, 0);
    }

    int total = 0;
    QTreeWidgetItemIterator it(tree);
    while (*it) {
        if (new_list.contains((*it)->text(8))) {
            ++total;
            (*it)->setHidden(false);
            (*it)->setText(6, "true"); // Displayed flag
        } else {
            (*it)->setHidden(true);
            (*it)->setText(6, "false");
            (*it)->setCheckState(0, Qt::Unchecked); // uncheck hidden items
        }
        ++it;
    }
    ui->labelNumAppFP->setText(QString::number(total));
}


// Display available packages
void MainWindow::displayPackages()
{
    qDebug() << "+++ Enter Function:" << __PRETTY_FUNCTION__ << "+++";

    QTreeWidget *newtree; // use this to not overwrite current "tree"

    QMap<QString, QStringList> list;
    if (tree == ui->treeMXtest) {
        list = mx_list;
        newtree = ui->treeMXtest;
    } else if (tree == ui->treeBackports) {
        list = backports_list;
        newtree = ui->treeBackports;
    } else { // for ui-treeStable, ui->treePopularApps, ui->treeFlatpak
        list = stable_list;
        newtree = ui->treeStable;
    }

    newtree->blockSignals(true);

    QHash<QString, VersionNumber> hashInstalled = listInstalledVersions();
    QString app_name;
    QString app_info;
    QString app_ver;
    QString app_desc;
    VersionNumber installed;
    VersionNumber candidate;

    QTreeWidgetItem *widget_item;



    // create a list of apps, create a hash with app_name, app_info
    QMap<QString, QStringList>::iterator i;
    for (i = list.begin(); i != list.end(); ++i) {
        // get size for newer flatpak versions

        app_name = i.key();
        app_ver = i.value().at(0);
        app_desc = i.value().at(1);

        widget_item = new QTreeWidgetItem(tree);
        widget_item->setCheckState(0, Qt::Unchecked);
        widget_item->setText(2, app_name);
        widget_item->setText(3, app_ver);
        widget_item->setText(4, app_desc);
        widget_item->setText(6, "true"); // all items are displayed till filtered
    }
    for (int i = 0; i < newtree->columnCount(); ++i) {
        newtree->resizeColumnToContents(i);
    }

    // process the entire list of apps
    QTreeWidgetItemIterator it(newtree);
    int upgr_count = 0;
    int inst_count = 0;

    // update tree
    while (*it) {
        app_name = (*it)->text(2);
        if (isFilteredName(app_name) && ui->checkHideLibs->isChecked()) {
            (*it)->setHidden(true);
        }
        app_ver = (*it)->text(3);
        installed = hashInstalled.value(app_name);
        candidate = VersionNumber(list.value(app_name).at(0));
        VersionNumber repo_candidate(app_ver); // candidate from the selected repo, might be different than the one from Stable

        (*it)->setIcon(1, QIcon()); // reset update icon
        if (installed.toString().isEmpty()) {
            for (int i = 0; i < newtree->columnCount(); ++i) {
                if (stable_list.contains(app_name)) {
                    (*it)->setToolTip(i, tr("Version ") + stable_list.value(app_name).at(0) + tr(" in stable repo"));
                } else {
                    (*it)->setToolTip(i, tr("Not available in stable repo"));
                }
            }
            (*it)->setText(5, "not installed");
        } else {
            inst_count++;
            if (installed >= repo_candidate) {
                for (int i = 0; i < newtree->columnCount(); ++i) {
                    (*it)->setForeground(2, QBrush(Qt::gray));
                    (*it)->setForeground(4, QBrush(Qt::gray));
                    (*it)->setToolTip(i, tr("Latest version ") + installed.toString() + tr(" already installed"));
                }
                (*it)->setText(5, "installed");
            } else {
                (*it)->setIcon(1, QIcon::fromTheme("software-update-available", QIcon(":/icons/software-update-available.png")));
                for (int i = 0; i < newtree->columnCount(); ++i) {
                    (*it)->setToolTip(i, tr("Version ") + installed.toString() + tr(" installed"));
                }
                upgr_count++;
                (*it)->setText(5, "upgradable");
            }

        }
        ++it;
    }
    updateInterface();
    newtree->blockSignals(false);
}

void MainWindow::displayFlatpaks(bool force_update)
{
    qDebug() << "+++ Enter Function:" << __PRETTY_FUNCTION__ << "+++";

    setCursor(QCursor(Qt::BusyCursor));
    ui->treeFlatpak->clear();
    ui->treeFlatpak->blockSignals(true);
    change_list.clear();

    if (flatpaks.isEmpty() || force_update) {
        blockInterfaceFP(true);
        flatpaks = listFlatpaks(ui->comboRemote->currentText());
        flatpaks_apps.clear();
        flatpaks_runtimes.clear();

        // list installed packages
        installed_apps_fp = listInstalledFlatpaks("--app");

        // add runtimes (needed for older flatpak versions)
        installed_runtimes_fp = listInstalledFlatpaks("--runtime");
    }

    QStringList installed_all = QStringList() << installed_apps_fp << installed_runtimes_fp;

    int total_count = 0;
    QTreeWidgetItem *widget_item;

    QString short_name, full_name, arch, version, size;
    foreach (QString item, flatpaks) {
        size = item.section("\t", 1);
        item = item.section("\t", 0, 0); // strip size
        full_name = item.section("/", 0, 0); // return first part of the name before slash
        short_name = full_name.section(".", -1);
        version = item.section("/", -1);
        if (short_name == "Locale" || short_name == "Sources" || short_name == "Debug") { // skip Locale, Sources, Debug
            continue;
        }
        ++total_count;
        widget_item = new QTreeWidgetItem(ui->treeFlatpak);
        widget_item->setCheckState(0, Qt::Unchecked);
        widget_item->setText(1, short_name);
        widget_item->setText(2, full_name);
        widget_item->setText(3, version);
        widget_item->setText(4, size);
        widget_item->setText(8, item); // Full string
        if (installed_all.contains(item)) {
            widget_item->setForeground(1, QBrush(Qt::gray));
            widget_item->setForeground(2, QBrush(Qt::gray));
            widget_item->setText(5, "installed");
        } else {
            widget_item->setText(5, "not installed");
        }
        widget_item->setText(6, "true"); // all items are displayed till filtered
    }

    // add sizes for the installed packages for older flatpak that doesn't list size for all the packages
    if (VersionNumber(getVersion("flatpak")) < VersionNumber("1.0.1")) {
        addInstalledSizesFP();
    }

    ui->labelNumAppFP->setText(QString::number(total_count));

    int total = 0;
    if (installed_apps_fp != QStringList("")) {
        total = installed_apps_fp.count();
    }
    ui->labelNumInstFP->setText(QString::number(total));

    ui->treeFlatpak->sortByColumn(1, Qt::AscendingOrder);

    removeDuplicatesFP();

    for (int i = 0; i < ui->treeFlatpak->columnCount(); ++i) {
        ui->treeFlatpak->resizeColumnToContents(i);
    }

    ui->searchBoxFlatpak->setFocus();
    ui->treeFlatpak->blockSignals(false);
    filterChanged(ui->comboFilterFlatpak->currentText());
    blockInterfaceFP(false);
}

// Display warning for Debian Backports
void MainWindow::displayWarning()
{
    qDebug() << "+++ Enter Function:" << __PRETTY_FUNCTION__ << "+++";
    if (warning_displayed) {
        return;
    }
    QFileInfo checkfile(QDir::homePath() + "/.config/mx-debian-backports-installer");
    if (checkfile.exists()) {
        return;
    }
    QMessageBox msgBox(QMessageBox::Warning,
                       tr("Warning"),
                       tr("You are about to use Debian Backports, which contains packages taken from the next "\
                          "Debian release (called 'testing'), adjusted and recompiled for usage on Debian stable. "\
                          "They cannot be tested as extensively as in the stable releases of Debian and MX Linux, "\
                          "and are provided on an as-is basis, with risk of incompatibilities with other components "\
                          "in Debian stable. Use with care!"), 0, 0);
    msgBox.addButton(QMessageBox::Close);
    QCheckBox *cb = new QCheckBox();
    msgBox.setCheckBox(cb);
    cb->setText(tr("Do not show this message again"));
    connect(cb, &QCheckBox::clicked, this, &MainWindow::disableWarning);
    msgBox.exec();
    warning_displayed = true;
}

// If dowload fails hide progress bar and show first tab
void MainWindow::ifDownloadFailed()
{
    qDebug() << "+++ Enter Function:" << __PRETTY_FUNCTION__ << "+++";
    progress->hide();
    ui->tabWidget->setCurrentWidget(ui->tabPopular);
}


// List the flatpak remote and loade them into combobox
void MainWindow::listFlatpakRemotes()
{
    qDebug() << "+++ Enter Function:" << __PRETTY_FUNCTION__ << "+++";
    ui->comboRemote->blockSignals(true);
    ui->comboRemote->clear();
    QStringList list = cmd->getOutput("su $(logname) -c \"flatpak remote-list " +  user + "| cut -f1\"").remove(" ").split("\n");
    ui->comboRemote->addItems(list);
    //set flathub default
    ui->comboRemote->setCurrentIndex(ui->comboRemote->findText("flathub"));
    ui->comboRemote->blockSignals(false);
}

// Install the list of apps
bool MainWindow::install(const QString &names)
{
    qDebug() << "+++ Enter Function:" << __PRETTY_FUNCTION__ << "+++";

    setConnections();
    if (!checkOnline()) {
        QMessageBox::critical(this, tr("Error"), tr("Internet is not available, won't be able to download the list of packages"));
        return false;
    }

    lock_file->unlock();
    ui->tabWidget->setTabText(ui->tabWidget->indexOf(ui->tabOutput), tr("Installing packages..."));

    if (tree == ui->treeBackports) {
        cmd->run("DEBIAN_FRONTEND=gnome apt-get install -t " + ver_name + "-backports --reinstall " + names + "| tee -a /var/log/mxpi.log");
    } else {
        cmd->run("DEBIAN_FRONTEND=gnome apt-get install --reinstall " + names + "| tee -a /var/log/mxpi.log");
    }
    lock_file->lock();

    return (cmd->getExitCode(true) == 0);
}

// install a list of application and run postprocess for each of them.
bool MainWindow::installBatch(const QStringList &name_list)
{
    qDebug() << "+++ Enter Function:" << __PRETTY_FUNCTION__ << "+++";
    QString postinstall;
    QString install_names;
    bool result = true;

    // load all the
    foreach (const QString &name, name_list) {
        foreach (const QStringList &list, popular_apps) {
            if (list.at(1) == name) {
                postinstall += list.at(6) + "\n";
                install_names += list.at(7) + " ";
            }
        }
    }

    if (!install_names.isEmpty()) {
        if (!install(install_names)) {
            result = false;
        }
    }
    if (postinstall != "\n") {
        qDebug() << "Post-install";
        setConnections();
        ui->tabWidget->setTabText(ui->tabWidget->indexOf(ui->tabOutput), tr("Post-processing..."));
        lock_file->unlock();
        if (cmd->run(postinstall, QStringList() << "slowtick") != 0) {
            result = false;
        }
    }
    lock_file->lock();
    return result;
}

// install named app
bool MainWindow::installPopularApp(const QString &name)
{
    qDebug() << "+++ Enter Function:" << __PRETTY_FUNCTION__ << "+++";
    int result = true;
    QString preinstall;
    QString postinstall;
    QString install_names;

    // get all the app info
    foreach (const QStringList &list, popular_apps) {
        if (list.at(1) == name) {
            preinstall = list.at(5);
            postinstall = list.at(6);
            install_names = list.at(7);
        }
    }

    // preinstall
    if (!preinstall.isEmpty()) {
        qDebug() << "Pre-install";
        setConnections();
        ui->tabWidget->setTabText(ui->tabWidget->indexOf(ui->tabOutput), tr("Pre-processing for ") + name);
        lock_file->unlock();
        if (cmd->run(preinstall) != 0) {
            QFile file("/etc/apt/sources.list.d/mxpitemp.list"); // remove temp source list if it exists
            if (file.exists()) {
                file.remove();
                update();
            }
            return false;
        }
    }

    // install
    if (!install_names.isEmpty()) {
        ui->tabWidget->setTabText(ui->tabWidget->indexOf(ui->tabOutput), tr("Installing ") + name);
        result = install(install_names);
    }

    // postinstall
    if (!postinstall.isEmpty()) {
        qDebug() << "Post-install";
        setConnections();
        ui->tabWidget->setTabText(ui->tabWidget->indexOf(ui->tabOutput), tr("Post-processing for ") + name);
        lock_file->unlock();
        cmd->run(postinstall);
    }
    lock_file->lock();
    return result;
}


// Process checked items to install
bool MainWindow::installPopularApps()
{
    qDebug() << "+++ Enter Function:" << __PRETTY_FUNCTION__ << "+++";

    QStringList batch_names;
    bool result = true;

    if (!checkOnline()) {
        QMessageBox::critical(this, tr("Error"), tr("Internet is not available, won't be able to download the list of packages"));
        return false;
    }
    if (!updated_once) {
        update();
    }

    // make a list of apps to be installed together
    QTreeWidgetItemIterator it(ui->treePopularApps);
    while (*it) {
        if ((*it)->checkState(1) == Qt::Checked) {
            QString name = (*it)->text(2);
            foreach (const QStringList &list, popular_apps) {
                if (list.at(1) == name) {
                    QString preinstall = list.at(5);
                    if (preinstall.isEmpty()) {  // add to batch processing if there is not preinstall command
                        batch_names << name;
                        (*it)->setCheckState(1, Qt::Unchecked);
                    }
                }
            }
        }
        ++it;
    }
    if (!installBatch(batch_names)) {
        result = false;
    }

    // install the rest of the apps
    QTreeWidgetItemIterator iter(ui->treePopularApps);
    while (*iter) {
        if ((*iter)->checkState(1) == Qt::Checked) {
            if (!installPopularApp((*iter)->text(2))) {
                result = false;
            }
        }
        ++iter;
    }
    setCursor(QCursor(Qt::ArrowCursor));
    return result;
}

// Install selected items
bool MainWindow::installSelected()
{
    qDebug() << "+++ Enter Function:" << __PRETTY_FUNCTION__ << "+++";
    ui->tabWidget->setTabEnabled(ui->tabWidget->indexOf(ui->tabOutput), true);
    bool initiallyEnabled = false;
    QString names = change_list.join(" ");

    // change sources as needed
    if(tree == ui->treeMXtest) {
        if (cmd->run("grep -q '^deb.* test' /etc/apt/sources.list.d/mx.list") == 0) {  // enabled
            initiallyEnabled = true;
        } else if (cmd->run("grep -q '^#\\s*deb.* test' /etc/apt/sources.list.d/mx.list") == 0) { // commented out line
            cmd->run("sed -i '/^#*\\s*deb.* test/s/^#*//' /etc/apt/sources.list.d/mx.list"); // uncomment
        } else { // doesn't exist, add
            if (ver_name == "jessie") { // use 'mx15' for Stretch based MX, user version name for newer versions
                cmd->run("echo -e '\ndeb http://mxrepo.com/mx/testrepo/ mx15 test' >> /etc/apt/sources.list.d/mx.list");
            } else {
                cmd->run("echo -e '\ndeb http://mxrepo.com/mx/testrepo/ " + ver_name + " test' >> /etc/apt/sources.list.d/mx.list");
            }
        }
        update();
    } else if (tree == ui->treeBackports) {
        cmd->run("echo deb http://ftp.debian.org/debian " + ver_name + "-backports main contrib non-free>/etc/apt/sources.list.d/mxpm-temp.list");
        update();
    }
    progress->hide();
    bool result = install(names);
    if (tree == ui->treeBackports) {
        cmd->run("rm -f /etc/apt/sources.list.d/mxpm-temp.list");
        update();
    } else if (tree == ui->treeMXtest && !initiallyEnabled) {
        cmd->run("sed -i 's/.* test/#&/'  /etc/apt/sources.list.d/mx.list");  // comment out the line
        update();
    }
    change_list.clear();
    installed_packages = listInstalled();
    return result;
}


// check if the name is filtered (lib, dev, dbg, etc.)
bool MainWindow::isFilteredName(const QString &name) const
{
    return ((name.startsWith("lib") && !name.startsWith("libreoffice")) || name.endsWith("-dev") || name.endsWith("-dbg") || name.endsWith("-dbgsym"));
}

// Check if online
bool MainWindow::checkOnline() const
{
    return(system("wget -q --spider http://mxrepo.com >/dev/null 2>&1") == 0);
}

// Build the list of available packages from various source
bool MainWindow::buildPackageLists(bool force_download)
{
    qDebug() << "+++ Enter Function:" << __PRETTY_FUNCTION__ << "+++";
    clearUi();
    if (!downloadPackageList(force_download)) {
        ifDownloadFailed();
        return false;
    }
    if (!readPackageList(force_download)) {
        ifDownloadFailed();
        return false;
    }
    displayPackages();
    return true;
}

// Download the Packages.gz from sources
bool MainWindow::downloadPackageList(bool force_download)
{
    qDebug() << "+++ Enter Function:" << __PRETTY_FUNCTION__ << "+++";
    QString repo_name;

    if (!checkOnline()) {
        QMessageBox::critical(this, tr("Error"), tr("Internet is not available, won't be able to download the list of packages"));
        return false;
    }
    if (tmp_dir.isEmpty()) {
        tmp_dir = cmd->getOutput("mktemp -d /tmp/mxpm-XXXXXXXX");
    }
    QDir::setCurrent(tmp_dir);
    connect(cmd, &Cmd::runTime, this, &MainWindow::updateBar, Qt::UniqueConnection);  // processes runtime emited by Cmd to be used by a progress bar
    connect(cmd, &Cmd::started, this, &MainWindow::cmdStart, Qt::UniqueConnection);
    connect(cmd, &Cmd::finished, this, &MainWindow::cmdDone, Qt::UniqueConnection);
    progress->setLabelText(tr("Downloading package info..."));
    progCancel->setEnabled(true);

    if (stable_list.isEmpty() || force_download) {
        if (force_download) {
            progress->show();
            if (!update()) {
                return false;
            }
        }
        progress->show();
        AptCache cache;
        stable_list = cache.getCandidates();
        if (stable_list.isEmpty()) {
            update();
            AptCache cache;
            stable_list = cache.getCandidates();
        }
    }

    if (tree == ui->treeMXtest)  {
        if (!QFile(tmp_dir + "/mxPackages").exists() || force_download) {
            progress->show();

            if (ver_name == "jessie") { // repo name is 'mx15' for Strech, use Debian version name for later versions
                repo_name = "mx15";
            } else {
                repo_name = ver_name;
            }

            if (cmd->run("wget --append-output=/var/log/mxpi.log http://mxrepo.com/mx/testrepo/dists/" + repo_name + "/test/binary-" + arch +
                             "/Packages.gz -O mxPackages.gz && gzip -df mxPackages.gz") != 0) {
                QFile::remove(tmp_dir + "/mxPackages.gz");
                QFile::remove(tmp_dir + "/mxPackages");
                return false;
            }
        }

    } else if (tree == ui->treeBackports) {
        if (!QFile(tmp_dir + "/mainPackages").exists() ||
                !QFile(tmp_dir + "/contribPackages").exists() ||
                !QFile(tmp_dir + "/nonfreePackages").exists() || force_download) {
            progress->show();
            int err = cmd->run("wget --append-output=/var/log/mxpi.log --timeout=5 ftp://ftp.us.debian.org/debian/dists/" +
                               ver_name + "-backports/main/binary-" + arch + "/Packages.gz -O mainPackages.gz && gzip -df mainPackages.gz");
            if (err != 0 ) {
                QFile::remove(tmp_dir + "/mainPackages.gz");
                QFile::remove(tmp_dir + "/mainPackages");
                return false;
            }
            //cmd->run("sleep 3");
            err = cmd->run("wget --append-output=/var/log/mxpi.log --timeout=5 ftp://ftp.us.debian.org/debian/dists/" +
                           ver_name + "-backports/contrib/binary-" + arch + "/Packages.gz -O contribPackages.gz && gzip -df contribPackages.gz");
            if (err != 0 ) {
                QFile::remove(tmp_dir + "/contribPackages.gz");
                QFile::remove(tmp_dir + "/contribPackages");
                return false;
            }
            //cmd->run("sleep 3");
            err = cmd->run("wget --append-output=/var/log/mxpi.log --timeout=5 ftp://ftp.us.debian.org/debian/dists/" +
                           ver_name + "-backports/non-free/binary-" + arch + "/Packages.gz -O nonfreePackages.gz && gzip -df nonfreePackages.gz");
            if (err != 0 ) {
                QFile::remove(tmp_dir + "/nonfreePackages.gz");
                QFile::remove(tmp_dir + "/nonfreePackages");
                return false;
            }
            progCancel->setDisabled(true);
            cmd->run("cat mainPackages contribPackages nonfreePackages > allPackages");
        }
    }
    return true;
}

void MainWindow::enableTabs(bool enable)
{
    for (int tab = 0; tab < 5; ++tab) {
        ui->tabWidget->setTabEnabled(tab, enable);
    }
}

// Process downloaded *Packages.gz files
bool MainWindow::readPackageList(bool force_download)
{
    qDebug() << "+++ Enter Function:" << __PRETTY_FUNCTION__ << "+++";
    progCancel->setDisabled(true);
    // don't process if the lists are already populated
    if (!((tree == ui->treeStable && stable_list.isEmpty()) || (tree == ui->treeMXtest && mx_list.isEmpty())||
          (tree == ui->treeBackports && backports_list.isEmpty()) || force_download)) {
        return true;
    }

    QFile file;
    if (tree == ui->treeMXtest)  { // read MX Test list
        file.setFileName(tmp_dir + "/mxPackages");
        if(!file.open(QFile::ReadOnly)) {
            qDebug() << "Could not open file: " << file.fileName();
            return false;
        }
    } else if (tree == ui->treeBackports) {  // read Backports lsit
        file.setFileName(tmp_dir + "/allPackages");
        if(!file.open(QFile::ReadOnly)) {
            qDebug() << "Could not open file: " << file.fileName();
            return false;
        }
    }

    QString file_content = file.readAll();
    file.close();

    QStringList list = file_content.split("\n");

    QMap<QString, QStringList> map;
    QStringList package_list;
    QStringList version_list;
    QStringList description_list;

    foreach(QString line, list) {
        if (line.startsWith("Package: ")) {
            package_list << line.remove("Package: ");
        } else if (line.startsWith("Version: ")) {
            version_list << line.remove("Version: ");
        } else if (line.startsWith("Description: ")) {
            description_list << line.remove("Description: ");
        }
    }

    for (int i = 0; i < package_list.size(); ++i) {
        map.insert(package_list.at(i), QStringList() << version_list.at(i) << description_list.at(i));
    }

    if (tree == ui->treeMXtest)  {
        mx_list = map;
    } else if (tree == ui->treeBackports) {
        backports_list = map;
    }

    return true;
}

// Cancel download
void MainWindow::cancelDownload()
{
    qDebug() << "+++ Enter Function:" << __PRETTY_FUNCTION__ << "+++";
    cmd->terminate();
}

// Clear UI when building package list
void MainWindow::clearUi()
{
    qDebug() << "+++ Enter Function:" << __PRETTY_FUNCTION__ << "+++";

    blockSignals(true);
    ui->comboFilterStable->setCurrentIndex(0);
    ui->comboFilterMX->setCurrentIndex(0);
    ui->comboFilterBP->setCurrentIndex(0);

    ui->buttonCancel->setEnabled(true);
    ui->buttonInstall->setEnabled(false);
    ui->buttonUninstall->setEnabled(false);

    ui->searchBoxStable->clear();
    ui->searchBoxMX->clear();
    ui->searchBoxBP->clear();

    if (tree == ui->treeStable || tree == ui->treePopularApps) {
        ui->labelNumApps->clear();
        ui->labelNumInst->clear();
        ui->labelNumUpgr->clear();
        ui->treeStable->clear();
        ui->buttonUpgradeAll->setHidden(true);
    } else if (tree == ui->treeMXtest || tree == ui->treePopularApps) {
        ui->labelNumApps_2->clear();
        ui->labelNumInstMX->clear();
        ui->labelNumUpgrMX->clear();
        ui->treeMXtest->clear();
    } else if (tree == ui->treeBackports || tree == ui->treePopularApps ) {
        ui->labelNumApps_3->clear();
        ui->labelNumInstBP->clear();
        ui->labelNumUpgrBP->clear();
        ui->treeBackports->clear();
    }
    blockSignals(false);
}

// Copy QTreeWidgets
void MainWindow::copyTree(QTreeWidget *from, QTreeWidget *to) const
{
    qDebug() << "+++ Enter Function:" << __PRETTY_FUNCTION__ << "+++";
    to->clear();
    QTreeWidgetItem *item;
    QTreeWidgetItemIterator it(from);
    while (*it) {
        item = new QTreeWidgetItem();
        item = (*it)->clone();
        to->addTopLevelItem(item);
        ++it;
    }
}

// Cleanup environment when window is closed
void MainWindow::cleanup() const
{
    qDebug() << "+++ Enter Function:" << __PRETTY_FUNCTION__ << "+++";
    cmd->disconnect();
    if(!cmd->terminate()) {
        cmd->kill();
    }
    qDebug() << "removing lock";
    lock_file->unlock();
    QDir::setCurrent("/");
    if (tmp_dir.startsWith("/tmp/mxpm-")) {
        qDebug() << "removing tmp folder";
        system("rm -r " + tmp_dir.toUtf8());
    }
}

// Get version of the program
QString MainWindow::getVersion(const QString name) const
{
    return cmd->getOutput("dpkg -l "+ name + "| awk 'NR==6 {print $3}'", QStringList() << "quiet");
}

// Return true if all the packages listed are installed
bool MainWindow::checkInstalled(const QString &names) const
{
    if (names.isEmpty()) {
        return false;
    }
    foreach(const QString &name, names.split("\n")) {
        if (!installed_packages.contains(name.trimmed())) {
            return false;
        }
    }
    return true;
}

// Return true if all the packages in the list are installed
bool MainWindow::checkInstalled(const QStringList &name_list) const
{
    qDebug() << "+++ Enter Function:" << __PRETTY_FUNCTION__ << "+++";
    if (name_list.size() == 0) {
        return false;
    }
    foreach(const QString &name, name_list) {
        if (!installed_packages.contains(name)) {
            return false;
        }
    }
    return true;
}

// return true if all the items in the list are upgradable
bool MainWindow::checkUpgradable(const QStringList &name_list) const
{
    qDebug() << "+++ Enter Function:" << __PRETTY_FUNCTION__ << "+++";
    if (name_list.size() == 0) {
        return false;
    }
    QList<QTreeWidgetItem *> item_list;
    foreach(const QString &name, name_list) {
        item_list = tree->findItems(name, Qt::MatchExactly, 2);
        if (item_list.isEmpty()) {
            return false;
        }
        if (item_list.at(0)->text(5) != "upgradable") {
            return false;
        }
    }
    return true;
}


// Returns list of all installed packages
QStringList MainWindow::listInstalled() const
{
    qDebug() << "+++ Enter Function:" << __PRETTY_FUNCTION__ << "+++";
    QString str = cmd->getOutput("dpkg --get-selections | grep -v deinstall | cut -f1", QStringList() << "slowtick" << "quiet");
    str.remove(":i386");
    str.remove(":amd64");
    return str.split("\n");
}


// Return list flatpaks from current remote
QStringList MainWindow::listFlatpaks(const QString remote, const QString type)
{
    qDebug() << "+++ Enter Function:" << __PRETTY_FUNCTION__ << "+++";

    QStringList list;
    // need to specify arch for older version
    QString arch = "";
    if (VersionNumber(getVersion("flatpak")) < VersionNumber("1.0.1")) {
        arch = "--arch=" + cmd->getOutput("arch") + " ";
        // list packages, strip first part remote/ or app/ no size for old flatpak
        list = cmd->getOutput("su $(logname) -c \"flatpak -d remote-ls " + user + remote + " " + arch + type + "| cut -f1 | tr -s ' ' | cut -f1 -d' '|sed 's/^[^\\/]*\\///g' \"").split("\n");
    } else {
        // list size too
        list = cmd->getOutput("su $(logname) -c \"flatpak -d remote-ls " + user + remote + " " + arch + type + "| cut -f1,3 |tr -s ' ' | sed 's/^[^\\/]*\\///g' \"").split("\n");
    }

    if (cmd->getExitCode(true) != 0) {
        qDebug() << "Could not list packages from remote" << remote;
        return QStringList();
    }

    // build cache lists
    if (type == "--app") {
        flatpaks_apps = list;
    } else if (type == "--runtime") {
        flatpaks_runtimes = list;
    }
    return list;
}

// list installed flatpaks by type: apps, runtimes, or all (if no type is provided)
QStringList MainWindow::listInstalledFlatpaks(const QString type) const
{
    return cmd->getOutput("su $(logname) -c \"flatpak -d list " + user + type + "|cut -f1|cut -f1 -d' '\"").remove(" ").split("\n");
}


// return the visible tree
void MainWindow::setCurrentTree()
{
    qDebug() << "+++ Enter Function:" << __PRETTY_FUNCTION__ << "+++";
    QList<QTreeWidget *> list;
    list << ui->treePopularApps << ui->treeStable << ui->treeMXtest << ui->treeBackports << ui->treeFlatpak;

    foreach (QTreeWidget *item, list) {
        if (item->isVisible()) {
            tree = item;
            return;
        }
    }
}

QHash<QString, VersionNumber> MainWindow::listInstalledVersions()
{
    qDebug() << "+++ Enter Function:" << __PRETTY_FUNCTION__ << "+++";
    QString out = cmd->getOutput("dpkg -l | grep '^ii'", QStringList() << "quiet");
    QStringList list = out.split("\n");

    QString name;
    QStringList item;
    QHash<QString, VersionNumber> result;
    foreach (QString line, list) {
        item = line.split(QRegularExpression("\\s{2,}"));
        name = item.at(1);
        name.remove(":i386").remove(":amd64");
        result.insert(name, VersionNumber(item.at(2)));
    }
    return result;
}


// Things to do when the command starts
void MainWindow::cmdStart()
{
    setCursor(QCursor(Qt::BusyCursor));
    ui->lineEdit->setFocus();
}


// Things to do when the command is done
void MainWindow::cmdDone()
{
    setCursor(QCursor(Qt::ArrowCursor));
    bar->setValue(bar->maximum());
    cmd->disconnect();
}

// Disable Backports warning
void MainWindow::disableWarning(bool checked)
{
    qDebug() << "+++ Enter Function:" << __PRETTY_FUNCTION__ << "+++";
    if (checked) {
        system("touch " + QDir::homePath().toUtf8() + "/.config/mx-debian-backports-installer");
    }
}

// Display info when clicking the "info" icon of the package
void MainWindow::displayInfo(const QTreeWidgetItem *item, int column)
{
    qDebug() << "+++ Enter Function:" << __PRETTY_FUNCTION__ << "+++";
    if (column == 3 && item->childCount() == 0) {
        QString desc = item->text(4);
        QString install_names = item->text(5);
        QString title = item->text(2);
        QString msg = "<b>" + title + "</b><p>" + desc + "<p>" ;
        if (install_names != "") {
            msg += tr("Packages to be installed: ") + install_names;
        }
        QUrl url = item->text(7); // screenshot url

        if (!url.isValid() || url.isEmpty() || url.url() == "none") {
            qDebug() << "no screenshot for: " << title;
        } else {
            QNetworkAccessManager *manager = new QNetworkAccessManager(this);
            QNetworkReply *reply = manager->get(QNetworkRequest(url));

            QEventLoop loop;
            connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
            timer->start(5000);
            connect(timer, &QTimer::timeout, &loop, &QEventLoop::quit);
            ui->treePopularApps->blockSignals(true);
            loop.exec();
            timer->stop();
            ui->treePopularApps->blockSignals(false);

            if (reply->error())
            {
                qDebug() << "Download of " << url.url() << " failed: " << qPrintable(reply->errorString());
            } else {
                QImage image;
                QByteArray data;
                QBuffer buffer(&data);
                QImageReader imageReader(reply);
                image = imageReader.read();
                if (imageReader.error()) {
                    qDebug() << "loading screenshot: " << imageReader.errorString();
                } else {
                    image = image.scaled(QSize(200,300), Qt::KeepAspectRatioByExpanding);
                    image.save(&buffer, "PNG");
                    msg += QString("<p><img src='data:image/png;base64, %0'>").arg(QString(data.toBase64()));
                }
            }
        }
        QMessageBox info(QMessageBox::NoIcon, tr("Package info") , msg, QMessageBox::Close, this);
        info.exec();
    }
}

// Find package in view
void MainWindow::findPopular() const
{
    QTreeWidgetItemIterator it(ui->treePopularApps);
    QString word = ui->searchPopular->text();
    if (word.length() == 1) {
        return;
    }
    if (word.isEmpty()) {
        while (*it) {
            (*it)->setExpanded(false);
            ++it;
        }
        ui->treePopularApps->reset();
        for (int i = 0; i < 5; ++i) {
            ui->treePopularApps->resizeColumnToContents(i);
        }
        return;
    }
    QList<QTreeWidgetItem *> found_items = ui->treePopularApps->findItems(word, Qt::MatchContains|Qt::MatchRecursive, 2);
    found_items << ui->treePopularApps->findItems(word, Qt::MatchContains|Qt::MatchRecursive, 4);

    // hide/unhide items
    while (*it) {
        if ((*it)->childCount() == 0) { // if child
            if (found_items.contains(*it)) {
                (*it)->setHidden(false);
          } else {
                (*it)->parent()->setHidden(true);
                (*it)->setHidden(true);
            }
        }
        ++it;
    }

    // process found items
    foreach(QTreeWidgetItem* item, found_items) {
        if (item->childCount() == 0) { // if child, expand parent
            item->parent()->setExpanded(true);
            item->parent()->setHidden(false);
        } else {  // if parent, expand children
            item->setExpanded(true);
            item->setHidden(false);
            int count = item->childCount();
            for (int i = 0; i < count; ++i ) {
                item->child(i)->setHidden(false);
            }
        }
    }
    for (int i = 0; i < 5; ++i) {
        ui->treePopularApps->resizeColumnToContents(i);
    }
}

// Find packages in other sources
void MainWindow::findPackageOther()
{
    QString word;
    if (tree == ui->treeStable) {
        word = ui->searchBoxStable->text();
    } else if (tree == ui->treeMXtest) {
        word = ui->searchBoxMX->text();
    } else if (tree == ui->treeBackports) {
        word = ui->searchBoxBP->text();
    } else if (tree == ui->treeFlatpak) {
        word = ui->searchBoxFlatpak->text();
    }
    if (word.length() == 1) {
        return;
    }

    QList<QTreeWidgetItem *> found_items = tree->findItems(word, Qt::MatchContains, 2);
    if (tree != ui->treeFlatpak) { // treeFlatpak has a different column structure
        found_items << tree->findItems(word, Qt::MatchContains, 4);
    }
    QTreeWidgetItemIterator it(tree);
    while (*it) {
      if ((*it)->text(6) == "true" && found_items.contains(*it)) {
          (*it)->setHidden(false);
      } else {
          (*it)->setHidden(true);
      }
      // hide libs
      QString app_name = (*it)->text(2);
      if (isFilteredName(app_name) && ui->checkHideLibs->isChecked()) {
          (*it)->setHidden(true);
      }
      ++it;
    }
}

void MainWindow::showOutput()
{
    ui->outputBox->clear();
    ui->tabWidget->setTabEnabled(ui->tabWidget->indexOf(ui->tabOutput), true);
    ui->tabWidget->setCurrentWidget(ui->tabOutput);
    enableTabs(false);
}

// Install button clicked
void MainWindow::on_buttonInstall_clicked()
{
    qDebug() << "+++ Enter Function:" << __PRETTY_FUNCTION__ << "+++";

    showOutput();

    if (tree == ui->treePopularApps) {
        bool success = installPopularApps();
        if(stable_list.size() > 0) { // clear cache to update list if it already exists
            buildPackageLists();
        }
        if (success) {
            refreshPopularApps();
            QMessageBox::information(this, tr("Done"), tr("Processing finished successfully."));
            ui->tabWidget->setCurrentWidget(tree->parentWidget());
        } else {
            refreshPopularApps();
            QMessageBox::critical(this, tr("Error"), tr("Problem detected while installing, please inspect the console output."));
        }
    } else if (tree == ui->treeFlatpak) {
        setConnections();
        setCursor(QCursor(Qt::BusyCursor));
        if (cmd->run("su $(logname) -c \"socat SYSTEM:'flatpak install -y " + user + ui->comboRemote->currentText() + " " + change_list.join(" ") + "',stderr STDIO\"") == 0) {
            displayFlatpaks(true);
            indexFilterFP.clear();
            ui->comboFilterFlatpak->setCurrentIndex(0);
            QMessageBox::information(this, tr("Done"), tr("Processing finished successfully."));
            ui->tabWidget->blockSignals(true);
            ui->tabWidget->setCurrentWidget(ui->tabFlatpak);
            ui->tabWidget->blockSignals(false);
        } else {
            setCursor(QCursor(Qt::ArrowCursor));
            QMessageBox::critical(this, tr("Error"), tr("Problem detected while installing, please inspect the console output."));
        }
    } else {
        bool success = installSelected();
        buildPackageLists();
        refreshPopularApps();
        if (success) {
            QMessageBox::information(this, tr("Done"), tr("Processing finished successfully."));
            ui->tabWidget->setCurrentWidget(tree->parentWidget());
        } else {
            QMessageBox::critical(this, tr("Error"), tr("Problem detected while installing, please inspect the console output."));
        }
    }
    enableTabs(true);
}

// About button clicked
void MainWindow::on_buttonAbout_clicked()
{
    QMessageBox msgBox(QMessageBox::NoIcon,
                       tr("About MX Package Installer"), "<p align=\"center\"><b><h2>" +
                       tr("MX Package Installer") + "</h2></b></p><p align=\"center\">" + tr("Version: ") + version + "</p><p align=\"center\"><h3>" +
                       tr("Package Installer for MX Linux") +
                       "</h3></p><p align=\"center\"><a href=\"http://mxlinux.org\">http://mxlinux.org</a><br /></p><p align=\"center\">" +
                       tr("Copyright (c) MX Linux") + "<br /><br /></p>");
    QPushButton *btnLicense = msgBox.addButton(tr("License"), QMessageBox::HelpRole);
    QPushButton *btnChangelog = msgBox.addButton(tr("Changelog"), QMessageBox::HelpRole);
    QPushButton *btnCancel = msgBox.addButton(tr("Cancel"), QMessageBox::NoRole);
    btnCancel->setIcon(QIcon::fromTheme("window-close"));

    msgBox.exec();

    Cmd cmd;
    if (msgBox.clickedButton() == btnLicense) {
        QString user = cmd.getOutput("logname");
        if (system("command -v mx-viewer") == 0) { // use mx-viewer if available
            system("su " + user.toUtf8() + " -c \"mx-viewer file:///usr/share/doc/mx-packageinstaller/license.html '" + tr("MX Package Installer").toUtf8() + " " + tr("License").toUtf8() + "'\"&");
        } else {
            system("su " + user.toUtf8() + " -c \"xdg-open file:///usr/share/doc/mx-packageinstaller/license.html\"&");
        }
    } else if (msgBox.clickedButton() == btnChangelog) {
        QDialog *changelog = new QDialog(this);
        changelog->resize(600, 500);

        QTextEdit *text = new QTextEdit;
        text->setReadOnly(true);
        text->setText(cmd.getOutput("zless /usr/share/doc/" + QFileInfo(QCoreApplication::applicationFilePath()).fileName()  + "/changelog.gz"));

        QPushButton *btnClose = new QPushButton(tr("&Close"));
        btnClose->setIcon(QIcon::fromTheme("window-close"));
        connect(btnClose, &QPushButton::clicked, changelog, &QDialog::close);

        QVBoxLayout *layout = new QVBoxLayout;
        layout->addWidget(text);
        layout->addWidget(btnClose);
        changelog->setLayout(layout);
        changelog->exec();
    }
}
// Help button clicked
void MainWindow::on_buttonHelp_clicked()
{
    QLocale locale;
    QString lang = locale.bcp47Name();

    QString url = "https://mxlinux.org/wiki/help-files/help-mx-package-installer";

    if (lang.startsWith("fr")) {
        url = "https://mxlinux.org/wiki/help-files/help-mx-installateur-de-paquets";
    }
    Cmd c;
    QString user = c.getOutput("logname");
    QString exec = (system("command -v mx-viewer") == 0) ? "mx-viewer" : "xdg-open";   // use mx-viewer if available
    QString cmd = QString("su " + user + " -c \'" + exec + " \"%1\"\'&").arg(url);
    system(cmd.toUtf8());
}

// Resize columns when expanding
void MainWindow::on_treePopularApps_expanded()
{
    ui->treePopularApps->resizeColumnToContents(2);
    ui->treePopularApps->resizeColumnToContents(4);
}

// Tree item clicked
void MainWindow::on_treePopularApps_itemClicked()
{
    bool checked = false;
    bool installed = true;

    QTreeWidgetItemIterator it(ui->treePopularApps);
    while (*it) {
        if ((*it)->checkState(1) == Qt::Checked) {
            checked = true;
            if ((*it)->foreground(2) != Qt::gray) {
                installed = false;
            }
        }
        ++it;
    }
    ui->buttonInstall->setEnabled(checked);
    ui->buttonUninstall->setEnabled(checked && installed);
    if (checked && installed) {
        ui->buttonInstall->setText(tr("Reinstall"));
    } else {
        ui->buttonInstall->setText(tr("Install"));
    }
}

// Tree item expanded
void MainWindow::on_treePopularApps_itemExpanded(QTreeWidgetItem *item)
{
    item->setIcon(0, QIcon::fromTheme("folder-open"));
    ui->treePopularApps->resizeColumnToContents(2);
    ui->treePopularApps->resizeColumnToContents(4);
}

// Tree item collapsed
void MainWindow::on_treePopularApps_itemCollapsed(QTreeWidgetItem *item)
{
    item->setIcon(0, QIcon::fromTheme("folder"));
    ui->treePopularApps->resizeColumnToContents(2);
    ui->treePopularApps->resizeColumnToContents(4);
}


// Uninstall clicked
void MainWindow::on_buttonUninstall_clicked()
{
    qDebug() << "+++ Enter Function:" << __PRETTY_FUNCTION__ << "+++";

    showOutput();

    QString names;

    if (tree == ui->treePopularApps) {
        QTreeWidgetItemIterator it(ui->treePopularApps);
        while (*it) {
            if ((*it)->checkState(1) == Qt::Checked) {
                names += (*it)->text(6).replace("\n", " ") + " ";
            }
            ++it;
        }
    } else if (tree == ui->treeFlatpak) {
        QString cmd_str = "";
        bool success = true;

        // new version of flatpak takes a "-y" confirmation
        QString conf = "-y ";
        if (VersionNumber(getVersion("flatpak")) < VersionNumber("1.0.1")) {
            conf = "";
        }

        setCursor(QCursor(Qt::BusyCursor));
        foreach (QString app, change_list) {
            setConnections();
            if (cmd->run("su $(logname) -c \"socat SYSTEM:'flatpak uninstall " + conf + user + app + "',stderr STDIO\"") != 0) { // success if all processed successfuly, failure if one failed
                success = false;
            }
        }
        if (success) { // success if all processed successfuly, failure if one failed
            displayFlatpaks(true);
            indexFilterFP.clear();
            ui->comboFilterFlatpak->setCurrentIndex(0);
            QMessageBox::information(this, tr("Done"), tr("Processing finished successfully."));
            ui->tabWidget->blockSignals(true);
            ui->tabWidget->setCurrentWidget(ui->tabFlatpak);
            ui->tabWidget->blockSignals(false);
        } else {
            QMessageBox::critical(this, tr("Error"), tr("We encountered a problem uninstalling, please check output"));
        }
        enableTabs(true);
        return;
    } else {
        names = change_list.join(" ");
    }

    if (uninstall(names)) {
        if(stable_list.size() > 0) { // update list if it already exists
            buildPackageLists();
        }
        refreshPopularApps();
        QMessageBox::information(this, tr("Success"), tr("Processing finished successfully."));
        ui->tabWidget->setCurrentWidget(tree->parentWidget());
    } else {
        if(stable_list.size() > 0) { // update list if it already exists
            buildPackageLists();
        }
        refreshPopularApps();
        QMessageBox::critical(this, tr("Error"), tr("We encountered a problem uninstalling the program"));
    }
    enableTabs(true);
}

// Actions on switching the tabs
void MainWindow::on_tabWidget_currentChanged(int index)
{
    qDebug() << "+++ Enter Function:" << __PRETTY_FUNCTION__ << "+++";
    ui->tabWidget->setTabText(ui->tabWidget->indexOf(ui->tabOutput), tr("Console Output"));
    ui->buttonInstall->setEnabled(false);
    ui->buttonUninstall->setEnabled(false);

    // reset checkboxes when tab changes
    if (tree != ui->treePopularApps) {
        tree->blockSignals(true);
        tree->clearSelection();
        QTreeWidgetItemIterator it(tree);
        while (*it) {
             (*it)->setCheckState(0, Qt::Unchecked);
            ++it;
        }
        tree->blockSignals(false);
    }

    switch (index) {
    case 0:  // Popular
        enableTabs(true);
        setCurrentTree();
        ui->searchPopular->clear();
        ui->searchPopular->setFocus();
        break;
    case 1:  // Stable
        enableTabs(true);
        setCurrentTree();
        change_list.clear();
        if (tree->topLevelItemCount() == 0) {
            buildPackageLists();
        }
        ui->searchBoxStable->clear();
        ui->searchBoxStable->setFocus();
        break;
    case 2:  // Test
        enableTabs(true);
        setCurrentTree();
        change_list.clear();
        if (tree->topLevelItemCount() == 0) {
            buildPackageLists();
        }
        ui->searchBoxMX->clear();
        ui->searchBoxMX->setFocus();
        break;
    case 3:  // Backports
        enableTabs(true);
        setCurrentTree();
        displayWarning();
        change_list.clear();
        if (tree->topLevelItemCount() == 0) {
            buildPackageLists();
        }
        ui->searchBoxBP->clear();
        ui->searchBoxBP->setFocus();
        break;
    case 4: // Flatpak
        enableTabs(true);
        setCurrentTree();
        blockInterfaceFP(true);

        if(!checkInstalled("flatpak")) {
            int ans = QMessageBox::question(this, tr("Flatpak not installed"), tr("Flatpak is not currently installed.\nOK to go ahead and install it?"));
            if (ans == QMessageBox::No) {
                ui->tabWidget->setCurrentIndex(0);
                break;
            }
            ui->tabWidget->setTabEnabled(ui->tabWidget->indexOf(ui->tabOutput), true);
            ui->tabWidget->setCurrentWidget(ui->tabOutput);
            setCursor(QCursor(Qt::BusyCursor));
            install("flatpak");
            installed_packages = listInstalled();
            if (!checkInstalled("flatpak")) {
                QMessageBox::critical(this, tr("Flatpak not installed"), tr("Flatpak was not installed"));
                ui->tabWidget->setCurrentIndex(0);
                setCursor(QCursor(Qt::ArrowCursor));
                break;
            }
            if (ui->treeStable) { // mark flatpak installed in stable tree
                QHash<QString, VersionNumber> hashInstalled = listInstalledVersions();
                VersionNumber installed = hashInstalled.value("flatpak");
                QList<QTreeWidgetItem *> found_items  = ui->treeStable->findItems("flatpak", Qt::MatchExactly, 2);
                foreach (QTreeWidgetItem *item, found_items) {
                    for (int i = 0; i < ui->treeStable->columnCount(); ++i) {
                        item->setForeground(2, QBrush(Qt::gray));
                        item->setForeground(4, QBrush(Qt::gray));
                        item->setToolTip(i, tr("Latest version ") + installed.toString() + tr(" already installed"));
                    }
                    item->setText(5, "installed");
                }
            }
            cmd->run("su $(logname) -c \"flatpak remote-add --if-not-exists flathub https://flathub.org/repo/flathub.flatpakrepo\"");
            if (cmd->getExitCode(true) != 0) {
                QMessageBox::critical(this, tr("Flathub remote failed"), tr("Flathub remote could not be added"));
                ui->tabWidget->setCurrentIndex(0);
                setCursor(QCursor(Qt::ArrowCursor));
                break;
            }
            listFlatpakRemotes();
            displayFlatpaks(false);
            setCursor(QCursor(Qt::ArrowCursor));
            QMessageBox::warning(this, tr("Needs re-login"), tr("You might need to logout/login to see installed items in the menu"));
            ui->tabWidget->blockSignals(true);
            ui->tabWidget->setCurrentWidget(ui->tabFlatpak);
            ui->tabWidget->blockSignals(false);
            ui->tabWidget->setTabText(ui->tabWidget->indexOf(ui->tabOutput), tr("Console Output"));
            break;
        }
        setCursor(QCursor(Qt::BusyCursor));
        cmd->run("su $(logname) -c \"flatpak remote-add --if-not-exists flathub https://flathub.org/repo/flathub.flatpakrepo\"");
        if (cmd->getExitCode(true) != 0) {
            QMessageBox::critical(this, tr("Flathub remote failed"), tr("Flathub remote could not be added"));
            ui->tabWidget->setCurrentIndex(0);
            setCursor(QCursor(Qt::ArrowCursor));
            break;
        }
        setCursor(QCursor(Qt::ArrowCursor));
        if (ui->comboRemote->currentText().isEmpty()) {
            listFlatpakRemotes();
        }
        displayFlatpaks(false);
        break;
    case 5: // Output
        ui->buttonInstall->setDisabled(true);
        ui->buttonUninstall->setDisabled(true);
        break;
    }

    // display Upgrade All button
    ui->buttonUpgradeAll->setVisible((tree == ui->treeStable) && (ui->labelNumUpgr->text().toInt() > 0));
}

// Filter items according to selected filter
void MainWindow::filterChanged(const QString &arg1)
{
    qDebug() << "+++ Enter Function:" << __PRETTY_FUNCTION__ << "+++";
    QList<QTreeWidgetItem *> found_items;
    QTreeWidgetItemIterator it(tree);
    tree->blockSignals(true);

    // filter for Flatpak
    if (tree == ui->treeFlatpak) {
        if (arg1 == tr("Installed runtimes")) {
            displayFiltered(installed_runtimes_fp);
        } else if (arg1 == tr("Installed apps")) {
            displayFiltered(installed_apps_fp);
        } else if (arg1 == tr("All apps")) {
            if(flatpaks_apps.isEmpty()) {
                listFlatpaks(ui->comboRemote->currentText(), "--app");
            }
            displayFiltered(flatpaks_apps);
        } else if (arg1 == tr("All runtimes")) {
            if(flatpaks_runtimes.isEmpty()) {
                listFlatpaks(ui->comboRemote->currentText(), "--runtime");
            }
            displayFiltered(flatpaks_runtimes);
        } else if (arg1 == tr("All available")) {
            int total = 0;
            while (*it) {
                ++total;
                (*it)->setText(6, "true"); // Displayed flag
                (*it)->setHidden(false);
                ++it;
            }
            ui->labelNumAppFP->setText(QString::number(total));
        } else if (arg1 == tr("Not installed")) {
             found_items = tree->findItems("not installed", Qt::MatchExactly, 5);
             ui->labelNumAppFP->setText(QString::number(found_items.count()));
             while (*it) {
                 if (found_items.contains(*it) ) {
                     (*it)->setHidden(false);
                     (*it)->setText(6, "true"); // Displayed flag
                 } else {
                     (*it)->setHidden(true);
                     (*it)->setText(6, "false");
                     (*it)->setCheckState(0, Qt::Unchecked); // uncheck hidden items
                 }
                 ++it;
             }
        }
        setSearchFocus();
        findPackageOther();
        tree->blockSignals(false);
        return;
    }

    if (arg1 == tr("All packages")) {
        while (*it) {
            (*it)->setText(6, "true"); // Displayed flag
            (*it)->setHidden(false);
            ++it;
        }
        findPackageOther();
        setSearchFocus();
        tree->blockSignals(false);
        return;
    }

    if (arg1 == tr("Upgradable")) {
        found_items = tree->findItems("upgradable", Qt::MatchExactly, 5);
    } else if (arg1 == tr("Installed")) {
        found_items = tree->findItems("installed", Qt::MatchExactly, 5);
    } else if (arg1 == tr("Not installed")) {
        found_items = tree->findItems("not installed", Qt::MatchExactly, 5);
    }

    while (*it) {
        if (found_items.contains(*it) ) {
            (*it)->setHidden(false);
            (*it)->setText(6, "true"); // Displayed flag
        } else {
            (*it)->setHidden(true);
            (*it)->setText(6, "false");
            (*it)->setCheckState(0, Qt::Unchecked); // uncheck hidden items
        }
        ++it;
    }
    findPackageOther();
    setSearchFocus();
    tree->blockSignals(false);
}

// When selecting on item in the list
void MainWindow::on_treeStable_itemChanged(QTreeWidgetItem *item)
{
    buildChangeList(item);
}


void MainWindow::on_treeMXtest_itemChanged(QTreeWidgetItem *item)
{
    buildChangeList(item);
}

void MainWindow::on_treeBackports_itemChanged(QTreeWidgetItem *item)
{
    buildChangeList(item);
}

void MainWindow::on_treeFlatpak_itemChanged(QTreeWidgetItem *item)
{
    buildChangeList(item);
}

// Build the change_list when selecting on item in the tree
void MainWindow::buildChangeList(QTreeWidgetItem *item)
{
    qDebug() << "+++ Enter Function:" << __PRETTY_FUNCTION__ << "+++";
    /* if all apps are uninstalled (or some installed) -> enable Install, disable Uinstall
     * if all apps are installed or upgradable -> enable Uninstall, enable Install
     * if all apps are upgradable -> change Install label to Upgrade;
     */

    if (change_list.isEmpty() && indexFilterFP.isEmpty()) { // remember the Flatpak combo location first time this is called
        indexFilterFP = ui->comboFilterFlatpak->currentText();
    }

    QString newapp = QString(item->text(2));
    if (item->checkState(0) == Qt::Checked) {
        ui->buttonInstall->setEnabled(true);
        change_list.append(newapp);
    } else {
        change_list.removeOne(newapp);
    }

    if (tree != ui->treeFlatpak) {
        if (!checkInstalled(change_list)) {
            ui->buttonUninstall->setEnabled(false);
        } else {
            ui->buttonUninstall->setEnabled(true);
        }

        if (checkUpgradable(change_list)) {
            ui->buttonInstall->setText(tr("Upgrade"));
        } else {
            ui->buttonInstall->setText(tr("Install"));
        }
    } else { // for Flatpaks allow selection only of installed or not installed items so one clicks on an installed item only installed items should be displayed and the other way round
        ui->buttonInstall->setText(tr("Install"));
        if (item->text(5) == "installed") {
            if (indexFilterFP == "All apps") { // if "all apps" is selected
                ui->comboFilterFlatpak->setCurrentText(tr("Installed apps"));
            }
            ui->buttonUninstall->setEnabled(true);
            ui->buttonInstall->setEnabled(false);
        } else {
            ui->comboFilterFlatpak->setCurrentText(tr("Not installed"));
            ui->buttonUninstall->setEnabled(false);
            ui->buttonInstall->setEnabled(true);
        }
        if (change_list.isEmpty()) { // reset comboFilterFlatpak if nothing is selected
            ui->comboFilterFlatpak->setCurrentText(indexFilterFP);
            indexFilterFP.clear();
        }
    }

    if (change_list.isEmpty()) {
        ui->buttonInstall->setEnabled(false);
        ui->buttonUninstall->setEnabled(false);
    }
}


// Force repo upgrade
void MainWindow::on_buttonForceUpdateStable_clicked()
{
    buildPackageLists(true);
}

void MainWindow::on_buttonForceUpdateMX_clicked()
{
    buildPackageLists(true);
}

void MainWindow::on_buttonForceUpdateBP_clicked()
{
    buildPackageLists(true);
}

// Hide/unhide lib/-dev packages
void MainWindow::on_checkHideLibs_toggled(bool checked)
{
    ui->checkHideLibsMX->setChecked(checked);
    ui->checkHideLibsBP->setChecked(checked);

    QTreeWidgetItemIterator it(ui->treeStable);
    while (*it) {
        QString app_name = (*it)->text(2);
        if (isFilteredName(app_name) && checked) {
            (*it)->setHidden(true);
        } else {
            (*it)->setHidden(false);
        }
        ++it;
    }
    filterChanged(ui->comboFilterStable->currentText());
}

// Upgrade all packages (from Stable repo only)
void MainWindow::on_buttonUpgradeAll_clicked()
{
    qDebug() << "+++ Enter Function:" << __PRETTY_FUNCTION__ << "+++";
    showOutput();

    QString names;
    QTreeWidgetItemIterator it(ui->treeStable);
    QList<QTreeWidgetItem *> found_items;
    found_items = ui->treeStable->findItems("upgradable", Qt::MatchExactly, 5);

    while (*it) {
        if(found_items.contains(*it)) {
            names += (*it)->text(2) + " ";
        }
        ++it;
    }

    if (install(names)) {
        buildPackageLists();
        QMessageBox::information(this, tr("Done"), tr("Processing finished successfully."));
        ui->tabWidget->setCurrentWidget(tree->parentWidget());
    } else {
        buildPackageLists();
        QMessageBox::critical(this, tr("Error"), tr("Problem detected while installing, please inspect the console output."));
    }

    enableTabs(true);
}


// Pressing Enter or buttonEnter should do the same thing
void MainWindow::on_buttonEnter_clicked()
{
    on_lineEdit_returnPressed();
}

// Send the response to terminal process
void MainWindow::on_lineEdit_returnPressed()
{
    qDebug() << "+++ Enter Function:" << __PRETTY_FUNCTION__ << "+++";
    cmd->writeToProc(ui->lineEdit->text());
    cmd->writeToProc("\n");
    ui->lineEdit->clear();
    ui->lineEdit->setFocus();
}

void MainWindow::on_buttonCancel_clicked()
{
    if (cmd->isRunning()) {
        if (QMessageBox::warning(this, tr("Quit?"),
                                     tr("Process still running, quiting might leave the system in an instable state.<p><b>Are you sure you want to exit MX Package Installer?</b>"),
                                     tr("Yes"), tr("No")) == 1){
            return;
        }
    }
    return qApp->quit();
}


void MainWindow::on_checkHideLibsMX_clicked(bool checked)
{
    ui->checkHideLibs->setChecked(checked);
    ui->checkHideLibsBP->setChecked(checked);
}

void MainWindow::on_checkHideLibsBP_clicked(bool checked)
{
    ui->checkHideLibs->setChecked(checked);
    ui->checkHideLibsMX->setChecked(checked);
}


// on change flatpack remote
void MainWindow::on_comboRemote_activated(int)
{
    qDebug() << "+++ Enter Function:" << __PRETTY_FUNCTION__ << "+++";
    displayFlatpaks(true);
}

void MainWindow::on_buttonUpgradeFP_clicked()
{
    qDebug() << "+++ Enter Function:" << __PRETTY_FUNCTION__ << "+++";
    showOutput();
    setConnections();
    setCursor(QCursor(Qt::BusyCursor));

    if(cmd->run("su $(logname) -c \"socat SYSTEM:'flatpak update " + user.trimmed() + "',stderr STDIO\"") == 0) {
        displayFlatpaks(true);
        setCursor(QCursor(Qt::ArrowCursor));
        QMessageBox::information(this, tr("Done"), tr("Processing finished successfully."));
        ui->tabWidget->blockSignals(true);
        ui->tabWidget->setCurrentWidget(ui->tabFlatpak);
        ui->tabWidget->blockSignals(false);
    } else {
        setCursor(QCursor(Qt::ArrowCursor));
        QMessageBox::critical(this, tr("Error"), tr("Problem detected while installing, please inspect the console output."));
    }
    enableTabs(true);
}

void MainWindow::on_buttonRemotes_clicked()
{
    ManageRemotes *dialog = new ManageRemotes(this);
    dialog->exec();
    if (dialog->isChanged()) {
        listFlatpakRemotes();
        displayFlatpaks(true);
    }
    if (!dialog->getInstallRef().isEmpty()) {
        showOutput();
        setConnections();
        setCursor(QCursor(Qt::BusyCursor));
        if (cmd->run("su $(logname) -c \"socat SYSTEM:'flatpak install -y " + dialog->getUser() + "--from " + dialog->getInstallRef() + "',stderr STDIO\"") == 0) {
            listFlatpakRemotes();
            displayFlatpaks(true);
            setCursor(QCursor(Qt::ArrowCursor));
            QMessageBox::information(this, tr("Done"), tr("Processing finished successfully."));
            ui->tabWidget->blockSignals(true);
            ui->tabWidget->setCurrentWidget(ui->tabFlatpak);
            ui->tabWidget->blockSignals(false);
        } else {
            setCursor(QCursor(Qt::ArrowCursor));
            QMessageBox::critical(this, tr("Error"), tr("Problem detected while installing, please inspect the console output."));
        }
        enableTabs(true);
    }
}

void MainWindow::on_comboUser_activated(int index)
{
    if (index == 0) {
        user = "--system ";
    } else {
        user = "--user ";
        setCursor(QCursor(Qt::BusyCursor));
        cmd->run("su $(logname) -c \"flatpak --user remote-add --if-not-exists flathub https://flathub.org/repo/flathub.flatpakrepo\"");
        setCursor(QCursor(Qt::ArrowCursor));
    }
    listFlatpakRemotes();
    displayFlatpaks(true);
}

