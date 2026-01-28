/**********************************************************************
 *  mxpackageinstaller.h
 **********************************************************************
 * Copyright (C) 2017-2025 MX Authors
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
#pragma once

#include <QCommandLineParser>
#include <QAction>
#include <QFile>
#include <QFutureWatcher>
#include <QHash>
#include <QElapsedTimer>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QProcess>
#include <QProgressDialog>
#include <QSettings>
#include <QString>
#include <QTimer>

class QTreeWidget;
class QTreeWidgetItem;

#include "cmd.h"
#include "checkableheaderview.h"
#include "lockfile.h"
#include "remotes.h"
#include "versionnumber.h"
#include "models/packagemodel.h"
#include "models/packagefilterproxy.h"

namespace Ui
{
class MainWindow;
}

// Use Status and TreeCol from packagemodel.h

namespace Tab
{
enum { Repos, AUR, Flatpak, Output };
}

namespace FlatCol
{
enum { Check, Name, LongName, Version, Branch, Size, Status, Duplicate, FullName };
}

struct PackageInfo {
    QString version;
    QString description;
};


constexpr uint KiB = 1024;
constexpr uint MiB = KiB * 1024;
constexpr uint GiB = MiB * 1024;

class MainWindow : public QDialog
{
    Q_OBJECT

public:
    explicit MainWindow(const QCommandLineParser &argParser, QWidget *parent = nullptr);
    ~MainWindow() override;

protected:
    void keyPressEvent(QKeyEvent *event) override;

signals:
    void displayPackagesFinished();

private slots:
    void checkUncheckItem();
    void cleanup();
    void cmdDone();
    void cmdStart();
    void disableOutput();
    void enableOutput();
    void filterChanged(const QString &arg1);
    void findPackage();
    void outputAvailable(const QString &output);
    void showOutput();
    void updateBar();

    void selectAllUpgradable_toggled(bool checked);
    void comboRemote_activated(int index = 0);
    void comboUser_currentIndexChanged(int index);
    void lineEdit_returnPressed();
    void pushAbout_clicked();
    void pushCancel_clicked();
    void pushEnter_clicked();
    void pushForceUpdateMX_clicked();
    void pushForceUpdateRepo_clicked();
    void pushForceUpdateFP_clicked();
    void pushHelp_clicked();
    void pushInstall_clicked();
    void pushRemotes_clicked();
    void pushRemoveUnused_clicked();
    void pushUninstall_clicked();
    void pushUpgradeFP_clicked();
    void tabWidget_currentChanged(int index);
    void treeFlatpak_itemChanged(QTreeWidgetItem *item);
private:
    Ui::MainWindow *ui;

    QString indexFilterFP;
    bool dirtyAur {true};
    bool dirtyRepo {true};
    bool displayFlatpaksIsRunning {false};
    bool displayPackagesIsRunning {false};
    bool firstRunFP {true};
    bool updatedOnce {false};
    bool warningAur {false};
    bool warningFlatpaks {false};
    int savedComboIndex {0};

    Cmd cmd;
    LockFile lockFile {"/var/lib/pacman/db.lck"};
    QHash<QString, VersionNumber> listInstalledVersions();
    QIcon qiconInstalled;
    QIcon qiconUpgradable;
    QLocale locale;
    QHash<QString, VersionNumber> installedVersionsCache;
    QElapsedTimer installedVersionsCacheTimer;
    QHash<QString, PackageInfo> installedPackages;
    QHash<QString, PackageInfo> aurList;
    QHash<QString, PackageInfo> aurInstalledCache;
    QHash<QString, PackageInfo> repoList;
    QHash<QString, PackageInfo> repoAllList;
    QSet<QString> repoInstalledSet;
    QSet<QString> repoUpgradableSet;
    QSet<QString> repoAutoremovableSet;
    bool repoCacheValid {false};
    bool aurInstalledCacheValid {false};
    bool aurInstalledCacheLoading {false};
    int aurInstalledCacheEpoch {0};
    int aurInstalledCacheEpochInFlight {0};
    bool aurUpgradesLoading {false};
    int aurUpgradesEpochInFlight {0};
    bool installedPackagesLoading {false};
    QFutureWatcher<QHash<QString, PackageInfo>> installedPackagesWatcher;
    QFutureWatcher<QHash<QString, PackageInfo>> aurInstalledCacheWatcher;
    QFutureWatcher<QHash<QString, QString>> aurUpgradesWatcher;
    QProgressBar *bar {};
    QProgressDialog *progress {};
    QPushButton *pushCancel {};
    QSettings settings;
    QString arch;
    QString fpUser;
    QStringList changeList;
    QStringList flatpaks;
    QStringList flatpaksApps;
    QStringList flatpaksRuntimes;
    QStringList installedAppsFP;
    QStringList installedRuntimesFP;
    QStringList cachedInstalledFlatpaks; // Raw lines from flatpak list --columns=ref,size
    QString cachedInstalledScope;
    QHash<QString, QString> cachedInstalledSizeMap; // canonical ref -> size string
    mutable QStringList cachedFlatpakRemotes;
    mutable QString cachedFlatpakRemotesScope;
    mutable bool cachedFlatpakRemotesFetched {false};
    bool cachedInstalledFetched {false};
    QStringList cachedAutoremovable;
    bool cachedAutoremovableFetched {false};
    QString cachedParuPath;
    bool cachedParuPathFetched {false};
    bool holdProgressForRepoRefresh {false};
    bool holdProgressForFlatpakRefresh {false};
    bool flatpakCancelHidden {false};
    bool flatpakUiBlocked {false};
    bool suppressCmdOutput {false};
    QTimer timer;
    QTreeWidget *currentTree {}; // current/calling tree (Flatpak only)
    QTreeWidgetItem *lastItemClicked {};
    QAction *lineEditToggleMaskAction {};
    QAction *lineEditClearAction {};
    bool lineEditMasked {false};
    const QCommandLineParser &args;
    const QString elevate {"/usr/bin/pkexec "};

    QNetworkAccessManager manager;
    QNetworkReply *reply {};

    [[nodiscard]] QHash<QString, PackageInfo> listInstalled();
    [[nodiscard]] QString getArchOption() const;
    [[nodiscard]] QString getVersion(const QString &name) const;
    [[nodiscard]] QString mapArchToFormat(const QString &arch) const;
    [[nodiscard]] QStringList listFlatpaks(const QString &remote, const QString &type = QLatin1String("")) const;
    [[nodiscard]] QStringList listInstalledFlatpaks(const QString &type = QLatin1String(""));
    [[nodiscard]] QTreeWidgetItem *createFlatpakItem(const QString &item, const QStringList &installedAll) const;
    [[nodiscard]] PackageData createPackageData(const QString &name, const QString &version,
                                                const QString &description) const;
    [[nodiscard]] bool checkInstalled(const QVariant &names) const;
    [[nodiscard]] bool checkUpgradable(const QStringList &nameList) const;
    [[nodiscard]] bool isOnline();
    [[nodiscard]] bool isPackageInstallable(const QString &installable, const QString &modArch) const;

    bool buildPackageLists(bool forceDownload = false);
    bool confirmActions(const QString &names, const QString &action);

    bool install(const QString &names, int sourceTab = -1);
    bool installSelected(int sourceTab = -1);
    bool markKeep();
    bool uninstall(const QString &names, const QString &preUninstall = QLatin1String(""),
                   const QString &postUninstall = QLatin1String(""));
    bool updateRepos();
    QStringList getAutoremovablePackages();
    [[nodiscard]] static QString convert(quint64 bytes);
    [[nodiscard]] static quint64 convert(const QString &size);
    [[nodiscard]] static QString shellQuote(const QString &value);
    [[nodiscard]] static QString shellQuotePackageList(const QStringList &packages);
    void blockInterfaceFP(bool block);
    void buildChangeList(QTreeWidgetItem *item);
    void cancelDownload();
    void centerWindow();
    void clearUi();
    void displayFilteredFP(QStringList list, bool raw = false);
    void displayFlatpaks(bool forceUpdate = false);
    void displayPackages();
    void displayWarning(const QString &repo);
    void enableTabs(bool enable);
    void finalizeFlatpakDisplay();
    void formatFlatpakTree() const;
    void handleRepoTab(const QString &searchStr);
    void handleAurTab(const QString &searchStr);
    void handleFlatpakTab(const QString &searchStr);
    void handleOutputTab();
    void hideColumns() const;
    void installFlatpak();
    void invalidateFlatpakRemoteCache();
    void listFlatpakRemotes() const;
    void listSizeInstalledFP();
    void loadFlatpakData();
    void populateFlatpakTree();
    void removeDuplicatesFP() const;
    void resetCheckboxes();
    void saveSearchText(QString &searchStr, int &filterIdx);
    void setConnections() const;
    void setCurrentTree();
    void setDirty();
    void setIcons();
    void setProgressDialog();
    void setSearchFocus() const;
    void setup();
    void setupFlatpakDisplay();
    void setLineEditMasked(bool masked);
    void startInstalledPackagesLoad();
    void startAurInstalledCacheLoad();
    void startAurUpgradesLoad();
    void updateRepoSetsFromInstalled();
    void updateFlatpakCounts(uint totalCount);
    void updateInterface() const;
    bool buildAurList(const QString &searchTerm);
    bool buildRepoCache(bool showProgress);
    void applyRepoFilter(int statusFilter);
    bool validateSudoPassword(QByteArray *passwordOut = nullptr);
    bool promptSudoPassword(QByteArray *passwordOut);
    void onAurSearchTextChanged();
    QString getParuPath();
    // Header checkbox helpers
    CheckableHeaderView *headerAUR {nullptr};
    CheckableHeaderView *headerRepo {nullptr};
    // Model/Proxy for QTreeView
    PackageModel *repoModel {nullptr};
    PackageFilterProxy *repoProxy {nullptr};
    PackageModel *aurModel {nullptr};
    PackageFilterProxy *aurProxy {nullptr};
};
