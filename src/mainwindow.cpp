/**********************************************************************
 *  MainWindow.cpp
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

#include "mainwindow.h"
#include "ui_mainwindow.h"

#include <QDebug>
#include <QFileInfo>
#include <QMenu>
#include <QNetworkProxyFactory>
#include <QProgressBar>
#include <QScreen>
#include <QScopedValueRollback>
#include <QScrollBar>
#include <QShortcut>
#include <QSignalBlocker>
#include <QStandardPaths>
#include <QSysInfo>
#include <QDialog>
#include <QDialogButtonBox>
#include <QElapsedTimer>
#include <QLabel>
#include <QLineEdit>
#include <QTextBlock>
#include <QTextStream>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrent>
#include <QtGlobal>

#include "about.h"
#include "checkableheaderview.h"
#include "versionnumber.h"
#include <algorithm>
#include <chrono>

using namespace std::chrono_literals;

namespace {
QString sanitizeOutputForDisplay(const QString &output)
{
    static const QRegularExpression ansiEscape {R"(\x1B\[[0-9;?]*[A-Za-z])"};
    static const QRegularExpression ansiQuery {R"(\x1B\[[0-9;?]*n)"};
    QString cleanOutput = output;
    cleanOutput.remove(ansiEscape);
    cleanOutput.remove(ansiQuery);
    return cleanOutput;
}
} // namespace

MainWindow::MainWindow(const QCommandLineParser &argParser, QWidget *parent)
    : QDialog(parent),
      ui(new Ui::MainWindow),
      args {argParser}
{
    qDebug().noquote() << QCoreApplication::applicationName() << "version:" << QCoreApplication::applicationVersion();
    ui->setupUi(this);
    setProgressDialog();

    connect(&timer, &QTimer::timeout, this, &MainWindow::updateBar);
    connect(&cmd, &Cmd::started, this, &MainWindow::cmdStart);
    connect(&cmd, &Cmd::done, this, &MainWindow::cmdDone);
    connect(&cmd, &Cmd::outputAvailable, this, [this](const QString &out) {
        if (!suppressCmdOutput) {
            qDebug() << sanitizeOutputForDisplay(out).trimmed();
        }
    });
    connect(&cmd, &Cmd::errorAvailable, this,
            [this](const QString &out) {
                if (!suppressCmdOutput) {
                    qWarning() << sanitizeOutputForDisplay(out).trimmed();
                }
            });
    setWindowFlags(Qt::Window); // For the close, min and max buttons

    setup();

    connect(&installedPackagesWatcher, &QFutureWatcher<QHash<QString, PackageInfo>>::finished, this, [this] {
        installedPackages = installedPackagesWatcher.result();
        installedPackagesLoading = false;
        updateRepoSetsFromInstalled();
        if (currentTree == ui->treeRepo && repoCacheValid) {
            applyRepoFilter(ui->comboFilterRepo->currentIndex());
            QMetaObject::invokeMethod(this, [this] { findPackage(); }, Qt::QueuedConnection);
            updateInterface();
        }
    });

    connect(&aurInstalledCacheWatcher, &QFutureWatcher<QHash<QString, PackageInfo>>::finished, this, [this] {
        aurInstalledCacheLoading = false;
        if (aurInstalledCacheEpochInFlight != aurInstalledCacheEpoch || aurInstalledCacheValid) {
            return;
        }
        aurInstalledCache = aurInstalledCacheWatcher.result();
        aurInstalledCacheValid = true;
    });

    connect(&aurUpgradesWatcher, &QFutureWatcher<QHash<QString, QString>>::finished, this, [this] {
        aurUpgradesLoading = false;
        if (aurUpgradesEpochInFlight != aurInstalledCacheEpoch || !aurInstalledCacheValid) {
            return;
        }
        const auto updates = aurUpgradesWatcher.result();
        if (updates.isEmpty()) {
            return;
        }
        for (auto it = updates.cbegin(); it != updates.cend(); ++it) {
            if (aurInstalledCache.contains(it.key())) {
                aurInstalledCache[it.key()].version = it.value();
            }
        }
        if (currentTree == ui->treeAUR && ui->searchBoxAUR->text().trimmed().isEmpty()) {
            for (auto it = updates.cbegin(); it != updates.cend(); ++it) {
                if (aurList.contains(it.key())) {
                    aurList[it.key()].version = it.value();
                }
            }
            QSignalBlocker blocker(currentTree);
            for (QTreeWidgetItemIterator it(currentTree); (*it) != nullptr; ++it) {
                const QString name = (*it)->text(TreeCol::Name);
                const auto updateIt = updates.constFind(name);
                if (updateIt != updates.constEnd()) {
                    (*it)->setText(TreeCol::RepoVersion, updateIt.value());
                }
            }
            updateTreeItems(currentTree);
            updateInterface();
        }
    });

    // Run flatpak setup and display in a separate thread
    if (arch != "i686" && checkInstalled("flatpak")) {
        auto flatpakFuture [[maybe_unused]] = QtConcurrent::run([this] {
            Cmd().run(elevate + "/usr/lib/mx-packageinstaller/mxpi-lib flatpak_add_repos", Cmd::QuietMode::Yes);
            QMetaObject::invokeMethod(this, [this] { displayFlatpaks(); }, Qt::QueuedConnection);
        });
    }
}

MainWindow::~MainWindow()
{
    delete ui;
}

void MainWindow::setup()
{
    qDebug() << "+++" << __PRETTY_FUNCTION__ << "+++";
    ui->tabWidget->blockSignals(true);
    ui->tabWidget->setCurrentWidget(ui->tabRepos);
    ui->tabWidget->setTabEnabled(Tab::Repos, true);
    ui->tabWidget->setTabEnabled(Tab::AUR, true);

    QFont font("monospace");
    font.setStyleHint(QFont::Monospace);
    ui->outputBox->setFont(font);

    QString defaultFSUser = settings.value("FlatpakUser", tr("For all users")).toString();
    fpUser = defaultFSUser == tr("For all users") ? "--system " : "--user ";
    ui->comboUser->blockSignals(true);
    ui->comboUser->setCurrentText(defaultFSUser);
    ui->comboUser->blockSignals(false);

    arch = QSysInfo::currentCpuArchitecture();

    ui->tabWidget->setTabVisible(Tab::Repos, true);
    ui->tabWidget->setTabVisible(Tab::AUR, true);
    ui->tabWidget->setTabVisible(Tab::Flatpak, arch != "i686");
    ui->tabWidget->setTabText(Tab::AUR, tr("AUR"));
    ui->tabWidget->setTabText(Tab::Repos, tr("Repositories"));

    setWindowTitle(tr("MX Package Installer"));
    setIcons();
    // Ensure "Select all" checkboxes start hidden/unchecked
    // (Deprecated UI checkboxes remain hidden in UI; header checkboxes are used instead.)
    if (auto *w = ui->checkSelectAllRepo) {
        w->setVisible(false);
        w->setChecked(false);
    }
    if (auto *w = ui->checkSelectAllMX) {
        w->setVisible(false);
        w->setChecked(false);
    }

    // Install custom header views with checkbox in column 0 (TreeCol::Check)
    headerRepo = new CheckableHeaderView(Qt::Horizontal, ui->treeRepo);
    headerRepo->setTargetColumn(TreeCol::Check);
    headerRepo->setMinimumSectionSize(22);
    ui->treeRepo->setHeader(headerRepo);

    headerAUR = new CheckableHeaderView(Qt::Horizontal, ui->treeAUR);
    headerAUR->setTargetColumn(TreeCol::Check);
    headerAUR->setMinimumSectionSize(22);
    ui->treeAUR->setHeader(headerAUR);

    hideColumns();
    setConnections();

    currentTree = ui->treeRepo;
    startInstalledPackagesLoad();
    startAurInstalledCacheLoad();
    buildRepoCache(false);
    applyRepoFilter(ui->comboFilterRepo->currentIndex());
    ui->searchBoxRepo->setFocus();

    ui->tabWidget->setTabEnabled(Tab::Output, false);
    ui->tabWidget->blockSignals(false);

    const auto size = this->size();
    if (settings.contains("geometry")) {
        restoreGeometry(settings.value("geometry").toByteArray());
        if (isMaximized()) { // Add option to resize if maximized
            resize(size);
            centerWindow();
        }
    }
    // Check/uncheck tree items spacebar press or double-click
    auto *shortcutToggle = new QShortcut(Qt::Key_Space, this);
    connect(shortcutToggle, &QShortcut::activated, this, &MainWindow::checkUncheckItem);

    QList listTree {ui->treeRepo, ui->treeAUR, ui->treeFlatpak};
    for (const auto &tree : listTree) {
        if (tree != ui->treeFlatpak) {
            tree->setContextMenuPolicy(Qt::CustomContextMenu);
        }
        connect(tree, &QTreeWidget::itemDoubleClicked, [tree](QTreeWidgetItem *item) { tree->setCurrentItem(item); });
        connect(tree, &QTreeWidget::itemDoubleClicked, this, &MainWindow::checkUncheckItem);
        connect(tree, &QTreeWidget::customContextMenuRequested, this,
                [this, tree](QPoint pos) { displayPackageInfo(tree, pos); });
    }
}

bool MainWindow::uninstall(const QString &names, const QString &preuninstall, const QString &postuninstall)
{
    qDebug() << "+++" << __PRETTY_FUNCTION__ << "+++";
    ui->tabWidget->setCurrentWidget(ui->tabOutput);

    bool success = true;
    // Simulate install of selections and present for confirmation
    // if user selects cancel, break routine but return success to avoid error message
    if (!confirmActions(names, "remove")) {
        return true;
    }

    ui->tabWidget->setTabText(Tab::Output, tr("Uninstalling packages..."));
    enableOutput();

    if (!preuninstall.isEmpty()) {
        qDebug() << "Pre-uninstall";
        ui->tabWidget->setTabText(Tab::Output, tr("Running pre-uninstall operations..."));
        enableOutput();
        if (lockFile.isLockedGUI()) {
            return false;
        }
        qDebug() << "Elevating preuninstall command:" << preuninstall;
        success = cmd.runAsRoot(preuninstall);
    }

    if (success) {
        enableOutput();
        if (lockFile.isLockedGUI()) {
            return false;
        }
        const QStringList nameList = names.split(' ', Qt::SkipEmptyParts);
        const QString quotedNames = shellQuotePackageList(nameList);
        qDebug() << "Elevating remove command:" << "pacman -Rns " + quotedNames;
        success = cmd.runAsRoot("pacman -Rns " + quotedNames);
    }

    if (success && !postuninstall.isEmpty()) {
        qDebug() << "Post-uninstall";
        ui->tabWidget->setTabText(Tab::Output, tr("Running post-uninstall operations..."));
        enableOutput();
        if (lockFile.isLockedGUI()) {
            return false;
        }
        qDebug() << "Elevating postuninstall command:" << postuninstall;
        success = cmd.runAsRoot(postuninstall);
    }
    return success;
}

bool MainWindow::updateRepos()
{
    qDebug() << "+++" << __PRETTY_FUNCTION__ << "+++";
    if (lockFile.isLockedGUI()) {
        return false;
    }
    ui->tabOutput->isVisible() // Don't display in output if calling to refresh from tabs
        ? ui->tabWidget->setTabText(Tab::Output, tr("Refreshing sources..."))
        : progress->show();
    if (!timer.isActive()) {
        timer.start(100ms);
    }

    enableOutput();
    qDebug() << "Elevating repo_sync command:" << "/usr/lib/mx-packageinstaller/mxpi-lib repo_sync";
    if (cmd.runAsRoot("/usr/lib/mx-packageinstaller/mxpi-lib repo_sync", Cmd::QuietMode::Yes)) {
        qDebug() << "repositories updated OK";
        updatedOnce = true;
        return true;
    }
    qDebug() << "problem updating repositories";
    QMessageBox::critical(this, tr("Error"),
                          tr("There was a problem updating repositories. Some sources may not have "
                             "provided updates. For more info check: ")
                              + "<a href=\"/var/log/mxpi.log\">/var/log/mxpi.log</a>");
    return false;
}

QStringList MainWindow::getAutoremovablePackages()
{
    if (cachedAutoremovableFetched) {
        return cachedAutoremovable;
    }
    cachedAutoremovable = cmd.getOut("pacman -Qtdq").split('\n', Qt::SkipEmptyParts);
    cachedAutoremovableFetched = true;
    return cachedAutoremovable;
}

// Convert different size units to bytes
quint64 MainWindow::convert(const QString &size)
{
    static const QMap<QString, quint64> multipliers {{"KB", KiB}, {"MB", MiB}, {"GB", GiB}};

    const QString number = size.section(QChar(160), 0, 0);
    const QString unit = size.section(QChar(160), 1).toUpper();
    const double value = number.toDouble();

    return static_cast<quint64>(value * multipliers.value(unit, 1)); // Default multiplier 1 for bytes
}

// Convert to string (#bytes, KiB, MiB, and GiB)
QString MainWindow::convert(quint64 bytes)
{
    auto size = static_cast<double>(bytes);
    if (bytes < KiB) {
        return QString::number(size) + " bytes";
    } else if (bytes < MiB) {
        return QString::number(size / KiB) + " KiB";
    } else if (bytes < GiB) {
        return QString::number(size / MiB, 'f', 1) + " MiB";
    } else {
        return QString::number(size / GiB, 'f', 2) + " GiB";
    }
}

// Quote a token for safe use inside a single-quoted shell context
// Replaces ' with '\'' (end quote, escaped quote, start quote)
// Returns a fully single-quoted token, suitable for use inside SYSTEM:'...'
QString MainWindow::shellQuote(const QString &value)
{
    QString escaped = value;
    escaped.replace('\'', QLatin1String("'\\''"));
    return QLatin1Char('\'') + escaped + QLatin1Char('\'');
}

// Quote a list of package names for safe use in shell commands
// Each package name is wrapped in single quotes after escaping
QString MainWindow::shellQuotePackageList(const QStringList &packages)
{
    QStringList quoted;
    quoted.reserve(packages.size());
    for (const QString &pkg : packages) {
        QString escaped = pkg;
        escaped.replace('\'', QLatin1String("'\\''"));
        quoted.append('\'' + escaped + '\'');
    }
    return quoted.join(' ');
}

void MainWindow::listSizeInstalledFP()
{
    qDebug() << "+++" << __PRETTY_FUNCTION__ << "+++";

    auto sumSizes = [](const QList<QString> &sizes) {
        return std::accumulate(sizes.cbegin(), sizes.cend(), quint64(0),
                               [](quint64 acc, const QString &item) { return acc + convert(item); });
    };

    quint64 total = 0;
    if (cachedInstalledScope == fpUser && cachedInstalledFetched) {
        total = sumSizes(cachedInstalledSizeMap.values());
    } else {
        const QString command = "flatpak list " + fpUser + "--columns app,size";
        QScopedValueRollback<bool> guard(suppressCmdOutput, true);
        QStringList list = cmd.getOut(command, Cmd::QuietMode::No).split('\n', Qt::SkipEmptyParts);
        total = std::accumulate(list.cbegin(), list.cend(), quint64(0),
                                [](quint64 acc, const QString &item) { return acc + convert(item.section('\t', 1)); });
    }
    ui->labelNumSize->setText(convert(total));
}

// Keep Flatpak UI enabled; rely on modal progress dialog to block interaction
void MainWindow::blockInterfaceFP(bool)
{
    // Maintain cursor feedback without toggling widget enabled state
    const bool isBusy = displayFlatpaksIsRunning;
    setCursor(isBusy ? QCursor(Qt::BusyCursor) : QCursor(Qt::ArrowCursor));
}

// Update interface when changing AUR tab
void MainWindow::updateInterface() const
{
    if (currentTree == ui->treeFlatpak) {
        return;
    }
    if (displayPackagesIsRunning) {
        connect(this, &MainWindow::displayPackagesFinished, this, &MainWindow::updateInterface, Qt::UniqueConnection);
        return;
    }
    QApplication::restoreOverrideCursor();
    progress->hide();
    int upgradeCount = 0;
    int installCount = 0;
    int visibleCount = 0;

    for (QTreeWidgetItemIterator it(currentTree); *it; ++it) {
        if ((*it)->isHidden()) {
            continue;
        }
        ++visibleCount;
        auto userData = (*it)->data(TreeCol::Status, Qt::UserRole).toInt();
        switch (userData) {
        case Status::Upgradable:
            ++upgradeCount;
            break;
        case Status::Installed:
            ++installCount;
            break;
        case Status::Autoremovable:
            ++installCount;
            break;
        }
    }

    auto updateLabelsAndFocus = [&](QLabel *labelNumApps, QLabel *labelNumUpgrade, QLabel *labelNumInstall,
                                    QPushButton *pushForceUpdate, QLineEdit *searchBox) {
        labelNumApps->setText(QString::number(visibleCount));
        labelNumUpgrade->setText(QString::number(upgradeCount));
        labelNumInstall->setText(QString::number(installCount + upgradeCount));
        pushForceUpdate->setEnabled(true);
        searchBox->setFocus();
    };

    if (ui->tabWidget->currentIndex() == Tab::Repos) {
        updateLabelsAndFocus(ui->labelNumAppsRepo, ui->labelNumUpgrRepo, ui->labelNumInstRepo,
                             ui->pushForceUpdateRepo, ui->searchBoxRepo);
    } else if (ui->tabWidget->currentIndex() == Tab::AUR) {
        updateLabelsAndFocus(ui->labelNumApps_2, ui->labelNumUpgrMX, ui->labelNumInstMX, ui->pushForceUpdateMX,
                             ui->searchBoxAUR);
    }
}

QString MainWindow::getArchOption() const
{
    static const QMap<QString, QString> archMap {
        {"x86_64", "--arch=x86_64"}, {"i686", "--arch=i386"}, {"arm", "--arch=arm"}, {"aarch64", "--arch=aarch64"}};
    return archMap.value(arch, QString()) + ' ';
}

void MainWindow::updateBar()
{
    QApplication::processEvents();
    bar->setValue((bar->value() + 1) % bar->maximum() + 1);
}

void MainWindow::checkUncheckItem()
{
    auto *currentTreeWidget = qobject_cast<QTreeWidget *>(focusWidget());

    if (!currentTreeWidget || !currentTreeWidget->currentItem() || currentTreeWidget->currentItem()->childCount() > 0) {
        return;
    }
    const auto col = static_cast<int>(TreeCol::Check);
    const auto newCheckState
        = (currentTreeWidget->currentItem()->checkState(col) == Qt::Checked) ? Qt::Unchecked : Qt::Checked;

    currentTreeWidget->currentItem()->setCheckState(col, newCheckState);
}

void MainWindow::outputAvailable(const QString &output)
{
    static const QRegularExpression statusKey {R"(^\s*(Installing|Uninstalling)\s+\d+/\d+)"};

    // Remove ANSI escape sequences
    QString cleanOutput = sanitizeOutputForDisplay(output);

    auto replaceLastStatusLine = [&](const QString &key, const QString &line) {
        QTextBlock block = ui->outputBox->document()->lastBlock();
        while (block.isValid()) {
            const QString text = block.text();
            if (text.trimmed().isEmpty()) {
                block = block.previous();
                continue;
            }
            const QRegularExpressionMatch match = statusKey.match(text);
            if (match.hasMatch() && match.captured(0) == key) {
                QTextCursor cursor(block);
                cursor.select(QTextCursor::LineUnderCursor);
                cursor.removeSelectedText();
                cursor.insertText(line);
                return true;
            }
            break;
        }
        return false;
    };

    auto insertLine = [&](const QString &line, bool addNewline, bool overwriteCurrentLine) {
        QTextCursor cursor = ui->outputBox->textCursor();
        cursor.movePosition(QTextCursor::End);
        if (overwriteCurrentLine) {
            cursor.movePosition(QTextCursor::StartOfLine, QTextCursor::KeepAnchor);
            cursor.removeSelectedText();
        }
        cursor.insertText(line);
        if (addNewline) {
            cursor.insertText("\n");
        }
        ui->outputBox->setTextCursor(cursor);
    };

    bool overwriteCurrentLine = false;
    QString buffer;
    auto flushBuffer = [&](bool addNewline) {
        if (buffer.isEmpty() && !addNewline) {
            return;
        }
        const QString line = buffer;
        buffer.clear();
        const QRegularExpressionMatch match = statusKey.match(line);
        const QString key = match.hasMatch() ? match.captured(0) : QString();
        const bool replaced = !key.isEmpty() && !overwriteCurrentLine && replaceLastStatusLine(key, line);
        if (!replaced) {
            insertLine(line, addNewline, overwriteCurrentLine);
        }
    };

    for (const QChar ch : cleanOutput) {
        if (ch == QLatin1Char('\r')) {
            flushBuffer(false);
            overwriteCurrentLine = true;
        } else if (ch == QLatin1Char('\n')) {
            flushBuffer(true);
            overwriteCurrentLine = false;
        } else {
            buffer.append(ch);
        }
    }
    flushBuffer(false);

    ui->outputBox->verticalScrollBar()->setValue(ui->outputBox->verticalScrollBar()->maximum());
}

namespace
{
struct ParsedFlatpakRef {
    QString ref;
    bool isRuntime {false};
};

QString canonicalFlatpakRef(const QString &ref)
{
    QString cleaned = ref.trimmed();
    if (cleaned.startsWith("app/") || cleaned.startsWith("runtime/")) {
        cleaned = cleaned.section('/', 1);
    }
    return cleaned;
}

bool isRuntimeToken(const QString &token)
{
    return token.startsWith("runtime/") || token.contains(".runtime/") || token.contains(".Platform");
}

ParsedFlatpakRef parseInstalledFlatpakLine(const QString &line)
{
    static const QRegularExpression refRegex(R"((app|runtime)/\S+)");

    const QRegularExpressionMatch match = refRegex.match(line);
    if (match.hasMatch()) {
        const QString refWithType = match.captured(0);
        return {refWithType.section('/', 1), refWithType.startsWith("runtime/")};
    }

    const QStringList tokens = line.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
    for (const QString &token : tokens) {
        if (token.contains('/')) {
            const bool isRuntime = isRuntimeToken(token);
            // If the token already lacks the app/runtime prefix, keep it intact
            const bool hasTypePrefix = token.startsWith("app/") || token.startsWith("runtime/");
            QString ref = hasTypePrefix ? token.section('/', 1) : token;
            return {ref.trimmed(), isRuntime};
        }
    }

    // Fallback: return the line as-is if it looks like a ref without type prefix
    const QString fallbackRef = line.contains('/') ? line.trimmed() : QString();
    const bool isRuntime = isRuntimeToken(fallbackRef);
    return {fallbackRef, isRuntime};
}

struct RemoteLsEntry {
    QString version;
    QString branch;
    QString ref;
    QString size;
};

RemoteLsEntry parseRemoteLsLine(const QString &line)
{
    RemoteLsEntry entry;

    const QStringList tabPartsRaw = line.split('\t', Qt::KeepEmptyParts);
    QStringList tabParts;
    tabParts.reserve(tabPartsRaw.size());
    for (const QString &part : tabPartsRaw) {
        tabParts.append(part.trimmed());
    }

    auto finalizeEntry = [&entry]() {
        if (entry.branch.isEmpty() && !entry.ref.isEmpty()) {
            entry.branch = entry.ref.section('/', -1);
        }
        if ((entry.version.isEmpty() || entry.version.contains('/')) && !entry.ref.isEmpty()) {
            entry.version = entry.ref.section('/', -1);
        }
        return entry;
    };

    if (tabParts.size() >= 4) {
        entry.version = tabParts.at(0);
        entry.branch = tabParts.at(1);
        entry.ref = tabParts.at(2);
        entry.size = tabParts.at(3);
        return finalizeEntry();
    }
    if (tabParts.size() == 3) {
        entry.version = tabParts.at(0);
        const QString possibleBranchOrRef = tabParts.at(1);
        if (possibleBranchOrRef.count('/') >= 2) {
            entry.ref = possibleBranchOrRef;
            entry.size = tabParts.at(2);
        } else {
            entry.branch = possibleBranchOrRef;
            entry.ref = tabParts.at(2);
        }
        return finalizeEntry();
    }

    QStringList tokens = line.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
    int refIndex = -1;
    for (int i = 0; i < tokens.size(); ++i) {
        if (tokens.at(i).count('/') >= 2) { // Looks like a Flatpak ref
            entry.ref = tokens.at(i);
            refIndex = i;
            break;
        }
    }

    if (!entry.ref.isEmpty()) {
        entry.version = tokens.value(0);
        entry.branch = tokens.value(1);
        if (refIndex >= 0 && refIndex + 1 < tokens.size()) {
            entry.size = tokens.mid(refIndex + 1).join(' ');
        }
        return finalizeEntry();
    }

    entry.ref = line.trimmed();
    return finalizeEntry();
}
} // namespace

// Handles duplicate Flatpak entries by adding context to their display names
void MainWindow::removeDuplicatesFP() const
{
    ui->treeFlatpak->setUpdatesEnabled(false);

    // First pass: identify duplicates
    QHash<QString, QList<QTreeWidgetItem *>> nameToItems;
    for (QTreeWidgetItemIterator it(ui->treeFlatpak); *it; ++it) {
        const QString name = (*it)->text(FlatCol::Name);
        nameToItems[name].append(*it);
    }

    // Second pass: rename duplicates with more context
    for (const auto &items : nameToItems) {
        if (items.size() > 1) {
            for (auto *item : items) {
                const QString longName = item->text(FlatCol::LongName);
                // Use the last two segments of the full name for better context
                const QString newName = longName.section('.', -2);
                item->setText(FlatCol::Name, newName);
            }
        }
    }

    ui->treeFlatpak->setUpdatesEnabled(true);
}

void MainWindow::setConnections() const
{
    connect(QApplication::instance(), &QApplication::aboutToQuit, this, &MainWindow::cleanup, Qt::QueuedConnection);
    // Connect search boxes
    connect(ui->searchBoxRepo, &QLineEdit::textChanged, this, &MainWindow::findPackage);
    connect(ui->searchBoxAUR, &QLineEdit::returnPressed, this, &MainWindow::findPackage);
    connect(ui->searchBoxAUR, &QLineEdit::textChanged, this, &MainWindow::onAurSearchTextChanged);
    connect(ui->searchBoxFlatpak, &QLineEdit::textChanged, this, &MainWindow::findPackage);

    // Update repo search hints
    ui->searchBoxRepo->setPlaceholderText(tr("Search repositories"));
    ui->searchBoxRepo->setToolTip(tr("Search repository packages by name or description"));

    // Update AUR search hints
    ui->searchBoxAUR->setPlaceholderText(tr("Search AUR (press Enter)"));
    ui->searchBoxAUR->setToolTip(tr("Enter search term and press Enter to search AUR packages"));

    // Connect combo filters
    connect(ui->comboFilterRepo, &QComboBox::currentTextChanged, this, &MainWindow::filterChanged);
    connect(ui->comboFilterAUR, &QComboBox::currentTextChanged, this, &MainWindow::filterChanged);
    connect(ui->comboFilterFlatpak, &QComboBox::currentTextChanged, this, &MainWindow::filterChanged);

    // Connect other UI elements to their respective slots
    connect(ui->comboRemote, QOverload<int>::of(&QComboBox::activated), this, &MainWindow::comboRemote_activated);
    connect(ui->comboUser, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &MainWindow::comboUser_currentIndexChanged);
    connect(ui->lineEdit, &QLineEdit::returnPressed, this, &MainWindow::lineEdit_returnPressed);
    connect(ui->pushAbout, &QPushButton::clicked, this, &MainWindow::pushAbout_clicked);
    connect(ui->pushCancel, &QPushButton::clicked, this, &MainWindow::pushCancel_clicked);
    connect(ui->pushEnter, &QPushButton::clicked, this, &MainWindow::pushEnter_clicked);
    connect(ui->pushForceUpdateRepo, &QPushButton::clicked, this, &MainWindow::pushForceUpdateRepo_clicked);
    connect(ui->pushForceUpdateMX, &QPushButton::clicked, this, &MainWindow::pushForceUpdateMX_clicked);
    connect(ui->pushForceUpdateFP, &QPushButton::clicked, this, &MainWindow::pushForceUpdateFP_clicked);
    connect(ui->pushHelp, &QPushButton::clicked, this, &MainWindow::pushHelp_clicked);
    connect(ui->pushInstall, &QPushButton::clicked, this, &MainWindow::pushInstall_clicked);
    connect(ui->pushRemotes, &QPushButton::clicked, this, &MainWindow::pushRemotes_clicked);
    connect(ui->pushRemoveUnused, &QPushButton::clicked, this, &MainWindow::pushRemoveUnused_clicked);
    connect(ui->pushUninstall, &QPushButton::clicked, this, &MainWindow::pushUninstall_clicked);
    connect(ui->pushUpgradeFP, &QPushButton::clicked, this, &MainWindow::pushUpgradeFP_clicked);
    connect(ui->tabWidget, QOverload<int>::of(&QTabWidget::currentChanged), this,
            &MainWindow::tabWidget_currentChanged);
    // Header checkbox (Upgradable): select all
    connect(headerRepo, &CheckableHeaderView::toggled, this, &MainWindow::selectAllUpgradable_toggled);
    connect(headerAUR, &CheckableHeaderView::toggled, this, &MainWindow::selectAllUpgradable_toggled);
    connect(ui->treeFlatpak, &QTreeWidget::itemChanged, this, &MainWindow::treeFlatpak_itemChanged);
    connect(ui->treeAUR, &QTreeWidget::itemChanged, this, &MainWindow::treeAUR_itemChanged);
    connect(ui->treeRepo, &QTreeWidget::itemChanged, this, &MainWindow::treeRepo_itemChanged);
}

void MainWindow::setProgressDialog()
{
    progress = new QProgressDialog(this);
    bar = new QProgressBar(progress);
    bar->setMaximum(bar->maximum());
    pushCancel = new QPushButton(tr("Cancel"));
    connect(pushCancel, &QPushButton::clicked, this, &MainWindow::cancelDownload);
    progress->setWindowModality(Qt::WindowModal);
    progress->setWindowFlags(Qt::Dialog | Qt::CustomizeWindowHint | Qt::WindowTitleHint | Qt::WindowStaysOnTopHint);
    progress->setCancelButton(pushCancel);
    pushCancel->setDisabled(true);
    progress->setLabelText(tr("Please wait..."));
    progress->setAutoClose(false);
    progress->setBar(bar);
    bar->setTextVisible(false);
    progress->reset();
}

void MainWindow::setSearchFocus() const
{
    static const QMap<int, QLineEdit *> searchBoxMap {{Tab::Repos, ui->searchBoxRepo},
                                                      {Tab::AUR, ui->searchBoxAUR},
                                                      {Tab::Flatpak, ui->searchBoxFlatpak}};
    const auto index = ui->tabWidget->currentIndex();
    if (auto *searchBox = searchBoxMap.value(index)) {
        searchBox->setFocus();
    }
}

void MainWindow::displayFilteredFP(QStringList list, bool raw)
{
    qDebug() << "+++" << __PRETTY_FUNCTION__ << "+++";
    ui->treeFlatpak->blockSignals(true);
    ui->treeFlatpak->setUpdatesEnabled(false);

    auto normalizeRef = [](const QString &line) {
        const RemoteLsEntry entry = parseRemoteLsLine(line);
        QString ref = entry.ref.trimmed();
        if (ref.startsWith("app/") || ref.startsWith("runtime/")) {
            ref = ref.section('/', 1); // Strip leading type segment (app/runtime)
        }
        return ref;
    };

    QMutableStringListIterator i(list);
    if (raw) { // Raw format that needs to be edited
        while (i.hasNext()) {
            i.setValue(normalizeRef(i.next()));
        }
    }

    auto normalizeForMatch = [](const QString &ref) { return canonicalFlatpakRef(ref); };

    QSet<QString> refSet;
    for (const QString &ref : std::as_const(list)) {
        refSet.insert(normalizeForMatch(ref));
    }

    uint total = 0;
    for (QTreeWidgetItemIterator it(currentTree); (*it) != nullptr; ++it) {
        const QString storedCanonical = (*it)->data(FlatCol::FullName, Qt::UserRole + 1).toString();
        const QString itemRef = normalizeForMatch(
            !storedCanonical.isEmpty() ? storedCanonical : (*it)->data(FlatCol::FullName, Qt::UserRole).toString());
        if (refSet.contains(itemRef)) {
            ++total;
            (*it)->setHidden(false);
            (*it)->setData(0, Qt::UserRole, true); // Displayed flag
            if ((*it)->checkState(FlatCol::Check) == Qt::Checked
                && (*it)->data(FlatCol::Status, Qt::UserRole) == Status::Installed) {
                ui->pushUninstall->setEnabled(true);
                ui->pushInstall->setEnabled(false);
            } else {
                ui->pushUninstall->setEnabled(false);
                ui->pushInstall->setEnabled(true);
            }
        } else {
            (*it)->setHidden(true);
            (*it)->setData(0, Qt::UserRole, false); // Displayed flag
            if ((*it)->checkState(FlatCol::Check) == Qt::Checked) {
                (*it)->setCheckState(FlatCol::Check, Qt::Unchecked); // Uncheck hidden item
                changeList.removeOne((*it)->data(FlatCol::FullName, Qt::UserRole).toString());
            }
        }
        if (changeList.isEmpty()) { // Reset comboFilterFlatpak if nothing is selected
            ui->pushUninstall->setEnabled(false);
            ui->pushInstall->setEnabled(false);
        }
    }
    if (lastItemClicked) {
        ui->treeFlatpak->scrollToItem(lastItemClicked);
    }
    ui->labelNumAppFP->setText(QString::number(total));
    ui->treeFlatpak->blockSignals(false);
    blockInterfaceFP(false);
    ui->treeFlatpak->setUpdatesEnabled(true);

    // Auto-adjust column widths after filter changes for Flatpak tab
    for (int i = 0; i < ui->treeFlatpak->columnCount(); ++i) {
        ui->treeFlatpak->resizeColumnToContents(i);
    }
}

void MainWindow::displayPackages()
{
    qDebug() << "+++" << __PRETTY_FUNCTION__ << "+++";

    displayPackagesIsRunning = true;

    if (currentTree != ui->treeAUR && currentTree != ui->treeRepo) {
        displayPackagesIsRunning = false;
        emit displayPackagesFinished();
        return;
    }
    auto *newTree = currentTree == ui->treeRepo ? ui->treeRepo : ui->treeAUR;
    auto *list = currentTree == ui->treeRepo ? &repoList : &aurList;

    QApplication::setOverrideCursor(Qt::WaitCursor);
    newTree->setUpdatesEnabled(false);
    newTree->blockSignals(true);

    newTree->clear();
    newTree->setSortingEnabled(false);
    newTree->addTopLevelItems(createTreeItemsList(list));
    newTree->sortItems(TreeCol::Name, Qt::AscendingOrder);

    updateTreeItems(newTree);
    newTree->blockSignals(false);
    newTree->setUpdatesEnabled(true);
    QApplication::restoreOverrideCursor();

    displayPackagesIsRunning = false;
    emit displayPackagesFinished();
}

QList<QTreeWidgetItem *> MainWindow::createTreeItemsList(QHash<QString, PackageInfo> *list) const
{
    QList<QTreeWidgetItem *> items;
    items.reserve(list->size());

    for (auto it = list->constBegin(); it != list->constEnd(); ++it) {
        items.append(createTreeItem(it.key(), it.value().version, it.value().description));
    }

    return items;
}

void MainWindow::updateTreeItems(QTreeWidget *tree)
{
    tree->setUpdatesEnabled(false);

    const auto installedVersions = listInstalledVersions();

    // Optimization: Pre-cache VersionNumber objects for repo versions to avoid repeated parsing
    QHash<QString, VersionNumber> repoVersionCache;
    repoVersionCache.reserve(tree->topLevelItemCount() * 2);

    for (QTreeWidgetItemIterator it(tree); *it; ++it) {
        auto *item = *it;
        const QString &appName = item->text(TreeCol::Name);

        // Get installed version information
        const VersionNumber installedVersion = installedVersions.value(appName);
        const QString installedVersionStr = installedVersion.toString();

        // Update installed version text only if changed
        if (!installedVersionStr.isEmpty() && item->text(TreeCol::InstalledVersion) != installedVersionStr) {
            item->setText(TreeCol::InstalledVersion, installedVersionStr);
        }

        // Set status based on installation state
        if (installedVersionStr.isEmpty()) {
            item->setData(TreeCol::Status, Qt::UserRole, Status::NotInstalled);
        } else {
            // Optimization: Cache VersionNumber objects for repo versions
            const QString repoVersionStr = item->text(TreeCol::RepoVersion);
            VersionNumber repoVersion;

            auto cacheIt = repoVersionCache.find(repoVersionStr);
            if (cacheIt != repoVersionCache.end()) {
                repoVersion = cacheIt.value();
            } else {
                repoVersion = VersionNumber(repoVersionStr);
                repoVersionCache.insert(repoVersionStr, repoVersion);
            }

            // Compare versions and set appropriate icon
            const bool isUpToDate = installedVersion >= repoVersion;
            item->setIcon(TreeCol::Check, isUpToDate ? qiconInstalled : qiconUpgradable);
            item->setData(TreeCol::Status, Qt::UserRole, isUpToDate ? Status::Installed : Status::Upgradable);
        }
    }

    // Optimization: Defer column resizing until the end and only resize visible columns
    tree->setUpdatesEnabled(true); // Enable updates first so resizing is efficient

    for (int i = 0; i < tree->columnCount(); ++i) {
        if (!tree->isColumnHidden(i)) {
            tree->resizeColumnToContents(i);
        }
    }
}

void MainWindow::displayFlatpaks(bool force_update)
{
    qDebug() << "+++" << __PRETTY_FUNCTION__ << "+++";
    if (!flatpaks.isEmpty() && !force_update) {
        return;
    }

    setupFlatpakDisplay();
    loadFlatpakData();
    populateFlatpakTree();
    finalizeFlatpakDisplay();
}

void MainWindow::setupFlatpakDisplay()
{
    ui->treeFlatpak->setUpdatesEnabled(false);
    displayFlatpaksIsRunning = true;
    lastItemClicked = nullptr;

    const bool isCurrentTabFlatpak = ui->tabWidget->currentIndex() == Tab::Flatpak;
    if (isCurrentTabFlatpak) {
        setCursor(QCursor(Qt::BusyCursor));
        if (!flatpakCancelHidden && pushCancel) {
            pushCancel->setEnabled(false);
            pushCancel->hide();
            flatpakCancelHidden = true;
        }
        progress->show();
        if (!timer.isActive()) {
            timer.start(100ms);
        }
    }

    listFlatpakRemotes();
    ui->treeFlatpak->blockSignals(true);
    ui->treeFlatpak->clear();
    changeList.clear();
    blockInterfaceFP(true);
}

void MainWindow::loadFlatpakData()
{
    flatpaks = listFlatpaks(ui->comboRemote->currentText());
    flatpaksApps.clear();
    flatpaksRuntimes.clear();
    QSet<QString> installedCanonical;
    cachedInstalledFlatpaks.clear();
    cachedInstalledSizeMap.clear();
    cachedInstalledScope.clear();
    cachedInstalledFetched = false;

    // Optimize: Get all installed packages with one command (ref + size), then split by type
    const QString allInstalledCommand = "flatpak list " + fpUser + "2>/dev/null --columns=ref,size";
    QScopedValueRollback<bool> guard(suppressCmdOutput, true);
    const QStringList allInstalled = cmd.getOut(allInstalledCommand, Cmd::QuietMode::No).split('\n', Qt::SkipEmptyParts);
    cachedInstalledFlatpaks = allInstalled;
    cachedInstalledScope = fpUser;
    cachedInstalledFetched = true;

    // Clear and reserve space for better performance
    installedAppsFP.clear();
    installedRuntimesFP.clear();
    installedAppsFP.reserve(allInstalled.size() / 2);
    installedRuntimesFP.reserve(allInstalled.size() / 2);

    // Split by type based on flatpak naming convention
    for (const QString &itemRaw : allInstalled) {
        if (itemRaw.startsWith("Ref")) { // header row on some versions
            continue;
        }

        const ParsedFlatpakRef parsed = parseInstalledFlatpakLine(itemRaw.section('\t', 0, 0));
        if (parsed.ref.isEmpty()) {
            continue;
        }

        const QString canonicalRef = canonicalFlatpakRef(parsed.ref);
        if (canonicalRef.isEmpty()) {
            continue;
        }
        installedCanonical.insert(canonicalRef);

        const QString sizeStr = itemRaw.section('\t', 1);
        if (!sizeStr.isEmpty()) {
            cachedInstalledSizeMap.insert(canonicalRef, sizeStr);
        }

        if (parsed.isRuntime) {
            installedRuntimesFP.append(canonicalRef);
        } else {
            installedAppsFP.append(canonicalRef);
        }
    }

    // Ensure installed refs are present in the display even if missing from remote listings
    QSet<QString> listedCanonicalRefs;
    for (const QString &entry : std::as_const(flatpaks)) {
        const RemoteLsEntry parsed = parseRemoteLsLine(entry);
        const QString canonical = canonicalFlatpakRef(parsed.ref);
        if (!canonical.isEmpty()) {
            listedCanonicalRefs.insert(canonical);
        }
    }

    const auto buildEntry = [](const QString &ref) {
        const QString branch = ref.section('/', -1);
        const QString version = branch;
        return version + '\t' + branch + '\t' + ref + '\t';
    };

    for (const QString &ref : std::as_const(installedCanonical)) {
        if (!listedCanonicalRefs.contains(ref)) {
            flatpaks.append(buildEntry(ref));
        }
    }

    // Build cached app/runtime lists from the already-fetched remote data to avoid re-querying
    flatpaksApps.clear();
    flatpaksRuntimes.clear();
    for (const QString &entry : std::as_const(flatpaks)) {
        const RemoteLsEntry parsed = parseRemoteLsLine(entry);
        const QString refForType = !parsed.ref.isEmpty() ? parsed.ref : canonicalFlatpakRef(parsed.ref);
        const bool isRuntime = isRuntimeToken(refForType) || isRuntimeToken(canonicalFlatpakRef(refForType));
        (isRuntime ? flatpaksRuntimes : flatpaksApps).append(entry);
    }
}

void MainWindow::populateFlatpakTree()
{
    const QStringList installed_all = installedAppsFP + installedRuntimesFP;
    uint total_count = 0;

    for (const QString &item : std::as_const(flatpaks)) {
        if (createFlatpakItem(item, installed_all)) {
            ++total_count;
        }
    }

    updateFlatpakCounts(total_count);
    formatFlatpakTree();
}

QTreeWidgetItem *MainWindow::createFlatpakItem(const QString &item, const QStringList &installed_all) const
{
    const RemoteLsEntry entry = parseRemoteLsLine(item);

    const QString originalRef = entry.ref.trimmed();
    QString ref = originalRef;
    const QString branch = entry.branch.isEmpty() ? ref.section('/', -1) : entry.branch;
    QString version = entry.version;
    if (version.isEmpty() || version.contains('/')) {
        version = branch;
    }
    const QString size = entry.size;
    const QString canonicalRef = canonicalFlatpakRef(ref);
    if (canonicalRef.isEmpty()) {
        return nullptr;
    }
    const QString long_name = canonicalRef.section('/', 0, 0);
    const QString short_name = long_name.section('.', -1);
    const QString name = canonicalRef;

    // Skip unwanted packages
    static const QSet<QString> unwantedPackages
        = {QLatin1String("Locale"), QLatin1String("Sources"), QLatin1String("Debug")};
    if (unwantedPackages.contains(short_name)) {
        return nullptr;
    }

    auto *widget_item = new QTreeWidgetItem(ui->treeFlatpak);
    widget_item->setCheckState(FlatCol::Check, Qt::Unchecked);
    widget_item->setText(FlatCol::Name, short_name);
    widget_item->setText(FlatCol::LongName, long_name);
    widget_item->setText(FlatCol::Version, version);
    widget_item->setText(FlatCol::Branch, branch);
    widget_item->setText(FlatCol::Size, size);
    widget_item->setData(FlatCol::FullName, Qt::UserRole, originalRef.isEmpty() ? name : originalRef);
    widget_item->setData(FlatCol::FullName, Qt::UserRole + 1, name); // canonical for matching
    widget_item->setData(0, Qt::UserRole, true);

    if (installed_all.contains(name)) {
        widget_item->setIcon(FlatCol::Check, QIcon::fromTheme("package-installed-updated",
                                                              QIcon(":/icons/package-installed-updated.png")));
        widget_item->setData(FlatCol::Status, Qt::UserRole, Status::Installed);
    } else {
        widget_item->setData(FlatCol::Status, Qt::UserRole, Status::NotInstalled);
    }

    return widget_item;
}

void MainWindow::updateFlatpakCounts(uint total_count)
{
    listSizeInstalledFP();
    ui->labelNumAppFP->setText(QString::number(total_count));
    ui->labelNumInstFP->setText(QString::number(!installedAppsFP.isEmpty() ? installedAppsFP.count() : 0));
}

void MainWindow::formatFlatpakTree() const
{
    ui->treeFlatpak->sortByColumn(FlatCol::Name, Qt::AscendingOrder);
    removeDuplicatesFP();

    for (int i = 0; i < ui->treeFlatpak->columnCount(); ++i) {
        ui->treeFlatpak->resizeColumnToContents(i);
    }
}

void MainWindow::finalizeFlatpakDisplay()
{
    ui->treeFlatpak->blockSignals(false);

    const bool isCurrentTabFlatpak = ui->tabWidget->currentIndex() == Tab::Flatpak;
    if (isCurrentTabFlatpak) {
        if (!ui->comboFilterFlatpak->currentText().isEmpty()) {
            filterChanged(ui->comboFilterFlatpak->currentText());
        }
        ui->searchBoxFlatpak->setFocus();
    }

    displayFlatpaksIsRunning = false;
    firstRunFP = false;
    blockInterfaceFP(false);
    if (holdProgressForFlatpakRefresh) {
        holdProgressForFlatpakRefresh = false;
        progress->hide();
    }
    if (flatpakCancelHidden && pushCancel) {
        pushCancel->show();
        pushCancel->setEnabled(true);
        flatpakCancelHidden = false;
    }
    ui->treeFlatpak->setUpdatesEnabled(true);
}

void MainWindow::displayWarning(const QString &repo)
{
    qDebug() << "+++" << __PRETTY_FUNCTION__ << "+++";

    bool *displayed = nullptr;
    QString msg;
    QString key;

    if (repo == "aur") {
        displayed = &warningAur;
        key = "NoWarningAur";
        msg = tr("You are about to use the AUR, which contains user-contributed packages. "
                 "These packages are not vetted by the distribution, may be outdated, and could "
                 "contain malicious or broken build scripts. Use with care.");

    } else if (repo == "flatpaks") {
        displayed = &warningFlatpaks;
        key = "NoWarningFlatpaks";
        msg = tr("Flatpak repositories are provided by third parties. "
                 "Use them at your own risk and consult upstream documentation for details.");
    }
    if ((displayed == nullptr) || *displayed || settings.value(key, false).toBool()) {
        return;
    }

    QMessageBox msgBox(QMessageBox::Warning, tr("Warning"), msg);
    msgBox.addButton(QMessageBox::Close);
    auto *cb = new QCheckBox();
    msgBox.setCheckBox(cb);
    cb->setText(tr("Do not show this message again"));
    connect(cb, &QCheckBox::clicked, this, [this, key, cb] { settings.setValue(key, cb->isChecked()); });
    msgBox.exec();
    *displayed = true;
}

void MainWindow::invalidateFlatpakRemoteCache()
{
    cachedFlatpakRemotes.clear();
    cachedFlatpakRemotesScope.clear();
    cachedFlatpakRemotesFetched = false;
}

void MainWindow::listFlatpakRemotes() const
{
    qDebug() << "+++" << __PRETTY_FUNCTION__ << "+++";
    QString currentRemote = ui->comboRemote->currentText();
    ui->comboRemote->blockSignals(true);
    ui->comboRemote->clear();
    const bool isUserScope = fpUser.startsWith("--user");

    auto applyRemotes = [&](const QStringList &list) {
        ui->comboRemote->addItems(list);
        QString savedRemote = firstRunFP ? settings.value("FlatpakRemote", "flathub").toString() : currentRemote;
        ui->comboRemote->setCurrentText(savedRemote.isEmpty() ? "flathub" : savedRemote);
        ui->comboRemote->blockSignals(false);
    };

    if (cachedFlatpakRemotesFetched && cachedFlatpakRemotesScope == fpUser) {
        applyRemotes(cachedFlatpakRemotes);
        return;
    }

    auto fetchRemotes = [this](QStringList &outList) {
        Cmd shell;
        outList
            = shell.getOut("flatpak remote-list " + fpUser + "| cut -f1").remove(' ').split('\n', Qt::SkipEmptyParts);
        return shell.exitCode() == 0;
    };

    auto addUserRemotes = []() {
        Cmd addRemotes;
        return addRemotes.run("/usr/lib/mx-packageinstaller/mxpi-lib flatpak_add_repos_user", Cmd::QuietMode::Yes);
    };

    QStringList list;
    bool listOk = fetchRemotes(list);

    // If user scope listing failed (common when user has never set up flatpak), attempt to set up defaults
    if (!listOk && isUserScope) {
        qDebug() << "User remote-list failed; attempting to set up user remotes";
        if (addUserRemotes()) {
            listOk = fetchRemotes(list);
        }
    }

    // If no user remotes exist, set up the default ones
    if (list.isEmpty() && isUserScope) {
        qDebug() << "No flatpak remotes found for user, setting up default remotes";

        if (addUserRemotes()) {
            qDebug() << "Successfully set up flatpak remotes for user";

            // Re-fetch the remote list after setup
            listOk = fetchRemotes(list);
        } else {
            qDebug() << "Failed to set up flatpak remotes for user";
        }
    }

    if (!listOk) {
        ui->comboRemote->blockSignals(false);
        return;
    }

    cachedFlatpakRemotes = list;
    cachedFlatpakRemotesScope = fpUser;
    cachedFlatpakRemotesFetched = true;

    applyRemotes(list);
}

bool MainWindow::confirmActions(const QString &names, const QString &action)
{
    qDebug() << "+++" << __PRETTY_FUNCTION__ << "+++";
    qDebug() << "names" << names << "and" << changeList;
    QString msg;

    QStringList detailed_installed_names;
    QString detailed_to_install;
    QString detailed_removed_names;
    if (currentTree == ui->treeFlatpak && names != "flatpak") {
        detailed_installed_names = changeList;
    }

    if (currentTree != ui->treeFlatpak) {
        detailed_installed_names = changeList;
        detailed_installed_names.sort();
        qDebug() << "detailed installed names sorted " << detailed_installed_names;

        if (action == QLatin1String("install")) {
            detailed_to_install = detailed_installed_names.join('\n');
            if (!detailed_to_install.isEmpty()) {
                detailed_to_install.prepend(tr("Install") + '\n');
            }
        } else if (action == QLatin1String("remove")) {
            detailed_removed_names = detailed_installed_names.join('\n');
            if (!detailed_removed_names.isEmpty()) {
                detailed_removed_names.prepend(tr("Remove") + '\n');
            }
        }
    } else {
        if (action == QLatin1String("remove")) {
            detailed_removed_names = changeList.join('\n');
            detailed_to_install.clear();
        }
        if (action == QLatin1String("install")) {
            detailed_to_install = changeList.join('\n');
            detailed_removed_names.clear();
        }
    }

    msg = "<b>" + tr("The following packages were selected. Click Show Details for list of changes.") + "</b>";

    QMessageBox msgBox;
    msgBox.setText(msg);
    msgBox.setInformativeText('\n' + names);

    if (action == QLatin1String("install")) {
        msgBox.setDetailedText(detailed_to_install + '\n' + detailed_removed_names);
    } else {
        msgBox.setDetailedText(detailed_removed_names + '\n' + detailed_to_install);
    }

    // Find Detailed Info box and set heigth, set box height between 100 - 400 depending on length of content
    const auto min = 100;
    const auto max = 400;
    auto *const detailedInfo = msgBox.findChild<QTextEdit *>();
    const auto recommended = qMax(msgBox.detailedText().length() / 2, min); // Half of length is just guesswork
    const auto height = qMin(recommended, max);
    detailedInfo->setFixedHeight(height);

    msgBox.addButton(QMessageBox::Ok);
    msgBox.addButton(QMessageBox::Cancel);

    const auto width = 600;
    auto *horizontalSpacer = new QSpacerItem(width, 0, QSizePolicy::Minimum, QSizePolicy::Expanding);
    auto *layout = qobject_cast<QGridLayout *>(msgBox.layout());
    layout->addItem(horizontalSpacer, 0, 1);
    return msgBox.exec() == QMessageBox::Ok;
}

// Validate sudo password and cache credentials for paru
bool MainWindow::validateSudoPassword(QByteArray *passwordOut)
{
    qDebug() << "+++" << __PRETTY_FUNCTION__ << "+++";
    if (passwordOut) {
        passwordOut->fill('\0');
        passwordOut->clear();
    }

    // First check if sudo is already cached (valid for 5 minutes by default)
    QProcess checkSudo;
    checkSudo.start("/usr/bin/sudo", {"-n", "-v"});
    checkSudo.waitForFinished(1000);

    if (checkSudo.exitCode() == 0) {
        qDebug() << "Sudo credentials already cached";
        return true;
    }

    // Prompt for password
    QByteArray passwordBytes;
    if (!promptSudoPassword(&passwordBytes)) {
        qDebug() << "User cancelled password prompt";
        return false;
    }

    // Validate password and cache sudo credentials
    QProcess validateSudo;
    validateSudo.start("/usr/bin/sudo", {"-S", "-v"});
    if (!validateSudo.waitForStarted(1000)) {
        // Securely zero password before returning
        passwordBytes.fill('\0');
        passwordBytes.clear();
        QMessageBox::critical(this, tr("Authentication Failed"),
                            tr("Could not start sudo. Please check your system configuration."));
        return false;
    }
    validateSudo.write(passwordBytes + '\n');
    validateSudo.closeWriteChannel();
    validateSudo.waitForFinished(3000);

    QString output = validateSudo.readAllStandardOutput() + validateSudo.readAllStandardError();

    if (validateSudo.exitCode() != 0) {
        qDebug() << "Sudo validation failed:" << output;
        // Securely zero password before returning
        passwordBytes.fill('\0');
        passwordBytes.clear();
        QMessageBox::critical(this, tr("Authentication Failed"),
                            tr("Incorrect password or sudo not configured properly."));
        return false;
    }

    if (passwordOut) {
        *passwordOut = passwordBytes;
    }
    // Zero the local password copy
    passwordBytes.fill('\0');
    passwordBytes.clear();

    qDebug() << "Sudo credentials validated and cached";
    return true;
}

bool MainWindow::promptSudoPassword(QByteArray *passwordOut)
{
    if (!passwordOut) {
        return false;
    }

    QDialog dialog(this);
    dialog.setWindowTitle(tr("Sudo Password Required"));
    dialog.setModal(true);
    dialog.setMinimumWidth(450);

    auto *layout = new QVBoxLayout(&dialog);
    auto *label = new QLabel(tr("Paru needs sudo privileges for package installation.\n"
                                "Please enter your password:"), &dialog);
    label->setWordWrap(true);
    layout->addWidget(label);

    auto *passwordEdit = new QLineEdit(&dialog);
    passwordEdit->setEchoMode(QLineEdit::Password);
    layout->addWidget(passwordEdit);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    if (dialog.exec() != QDialog::Accepted) {
        passwordEdit->clear();
        return false;
    }

    // Best-effort: QLineEdit exposes QString, so convert immediately and clear widget.
    *passwordOut = passwordEdit->text().toUtf8();
    passwordEdit->clear();
    return !passwordOut->isEmpty();
}

bool MainWindow::install(const QString &names)
{
    qDebug() << "+++" << __PRETTY_FUNCTION__ << "+++";

    if (!isOnline()) {
        QMessageBox::critical(this, tr("Error"),
                              tr("Internet is not available, won't be able to download the list of packages"));
        return false;
    }
    ui->tabWidget->setTabText(Tab::Output, tr("Installing packages..."));

    // Simulate install of selections and present for confirmation
    // if user selects cancel, break routine but return success to avoid error message
    if (!confirmActions(names, "install")) {
        return true;
    }
    enableOutput();
    if (currentTree != ui->treeAUR && lockFile.isLockedGUI()) {
        return false;
    }
    if (currentTree == ui->treeAUR) {
        // Check if paru is installed for AUR packages
        const QString paruPath = getParuPath();
        if (paruPath.isEmpty()) {
            QMessageBox::critical(this, tr("Error"),
                                tr("paru is not installed.\n\n"
                                   "To install AUR packages, please install paru first:\n"
                                   "pacman -S paru\n\n"
                                   "Then try installing the AUR package again."));
            return false;
        }

        // For AUR packages (paru), validate sudo password first since paru calls sudo internally
        QByteArray sudoPassword;
        if (!validateSudoPassword(&sudoPassword)) {
            return false;
        }
        const QStringList nameList = names.split(' ', Qt::SkipEmptyParts);
        const QString quotedNames = shellQuotePackageList(nameList);
        const QString command = paruPath + " --sudoflags \"-S -p ''\" -S --needed " + quotedNames;
        bool result = cmd.runWithInput(command, sudoPassword + '\n');
        // Securely zero password after use
        sudoPassword.fill('\0');
        sudoPassword.clear();
        return result;
    } else {
        const QStringList nameList = names.split(' ', Qt::SkipEmptyParts);
        const QString quotedNames = shellQuotePackageList(nameList);
        return cmd.runAsRoot("pacman -S --needed " + quotedNames);
    }
}

bool MainWindow::installSelected()
{
    qDebug() << "+++" << __PRETTY_FUNCTION__ << "+++";
    ui->tabWidget->setTabEnabled(Tab::Output, true);
    QString names = changeList.join(' ');

    bool result = install(names);
    changeList.clear();
    installedPackages = listInstalled();
    return result;
}

bool MainWindow::markKeep()
{
    qDebug() << "+++" << __PRETTY_FUNCTION__ << "+++";
    ui->tabWidget->setTabEnabled(Tab::Output, true);
    const QString quotedNames = shellQuotePackageList(changeList);
    enableOutput();
    qDebug() << "Elevating markKeep command:" << "pacman -D --asexplicit " + quotedNames;
    return cmd.runAsRoot("pacman -D --asexplicit " + quotedNames);
}

bool MainWindow::isOnline()
{
    if (settings.value("skiponlinecheck", false).toBool() || args.isSet("skip-online-check")) {
        return true;
    }

    QNetworkRequest request;
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setRawHeader("User-Agent", QApplication::applicationName().toUtf8() + '/'
                                           + QApplication::applicationVersion().toUtf8() + " (linux-gnu)");

    auto error = QNetworkReply::NoError;
    for (const QString address : {"https://archlinux.org", "https://google.com"}) {
        error = QNetworkReply::NoError; // reset for each tried address
        QNetworkProxyQuery query {QUrl(address)};
        QList<QNetworkProxy> proxies = QNetworkProxyFactory::systemProxyForQuery(query);
        if (!proxies.isEmpty()) {
            manager.setProxy(proxies.first());
        }
        request.setUrl(QUrl(address));
        reply = manager.head(request);
        QEventLoop loop;
        connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
        connect(reply, &QNetworkReply::errorOccurred, &loop, &QEventLoop::quit);
        auto timeout = settings.value("timeout", 7000).toUInt();
        manager.setTransferTimeout(timeout);
        loop.exec();
        reply->disconnect();
        if (reply->error() == QNetworkReply::NoError) {
            reply->deleteLater();
            return true;
        }
        // Clean up failed reply before next iteration
        reply->deleteLater();
        reply = nullptr;
    }
    qDebug() << "No network detected:" << error;
    return false;
}

bool MainWindow::buildPackageLists(bool forceDownload)
{
    qDebug() << "+++" << __PRETTY_FUNCTION__ << "+++";
    if (forceDownload) {
        setDirty();
    }
    clearUi();
    if (currentTree == ui->treeRepo) {
        if (!repoCacheValid && !buildRepoCache(true)) {
            return false;
        }
        applyRepoFilter(ui->comboFilterRepo->currentIndex());
        dirtyRepo = false;
    } else if (currentTree == ui->treeAUR) {
        if (!buildAurList(ui->searchBoxAUR->text())) {
            return false;
        }
        dirtyAur = true;
    } else {
        return true;
    }
    displayPackages();
    return true;
}

void MainWindow::enableTabs(bool enable)
{
    for (int tab = 0; tab < ui->tabWidget->count() - 1; ++tab) { // Enable all except last (Console)
        ui->tabWidget->setTabEnabled(tab, enable);
    }
    ui->tabWidget->setTabVisible(Tab::Repos, true);
    ui->tabWidget->setTabVisible(Tab::AUR, true);
    ui->tabWidget->setTabVisible(Tab::Flatpak, arch != "i686");
    setCursor(QCursor(Qt::ArrowCursor));
}

void MainWindow::hideColumns() const
{
    ui->tabWidget->setCurrentIndex(Tab::Repos);
    ui->treeFlatpak->setColumnHidden(FlatCol::Branch, false);
    ui->treeRepo->hideColumn(TreeCol::Status);
    ui->treeAUR->hideColumn(TreeCol::Status);
    ui->treeFlatpak->hideColumn(FlatCol::Status);
    ui->treeFlatpak->hideColumn(FlatCol::Duplicate);
    ui->treeFlatpak->hideColumn(FlatCol::FullName);
}

void MainWindow::cancelDownload()
{
    qDebug() << "+++" << __PRETTY_FUNCTION__ << "+++";
    holdProgressForRepoRefresh = false;
    holdProgressForFlatpakRefresh = false;
    cmd.terminate();
}

void MainWindow::centerWindow()
{
    const auto screenGeometry = QApplication::primaryScreen()->geometry();
    const auto x = (screenGeometry.width() - width()) / 2;
    const auto y = (screenGeometry.height() - height()) / 2;
    move(x, y);
}

void MainWindow::clearUi()
{
    qDebug() << "+++" << __PRETTY_FUNCTION__ << "+++";
    blockSignals(true);

    // Configure buttons
    ui->pushCancel->setEnabled(true);
    ui->pushInstall->setEnabled(false);
    ui->pushUninstall->setEnabled(false);

    // Clear UI elements based on the current tree
    if (currentTree == ui->treeRepo) {
        ui->labelNumAppsRepo->clear();
        ui->labelNumInstRepo->clear();
        ui->labelNumUpgrRepo->clear();
        ui->treeRepo->clear();
    } else if (currentTree == ui->treeAUR) {
        ui->labelNumApps_2->clear();
        ui->labelNumInstMX->clear();
        ui->labelNumUpgrMX->clear();
        ui->treeAUR->clear();
    }

    // Reset all filter combos
    if (currentTree == ui->treeRepo) {
        ui->comboFilterRepo->setCurrentIndex(savedComboIndex);
    } else if (currentTree == ui->treeAUR) {
        ui->comboFilterAUR->setCurrentIndex(savedComboIndex);
    }
    ui->comboFilterFlatpak->setCurrentIndex(0);

    blockSignals(false);
}

void MainWindow::cleanup()
{
    qDebug() << "+++" << __PRETTY_FUNCTION__ << "+++";
    if (cmd.state() != QProcess::NotRunning) {
        qDebug() << "Command" << cmd.program() << cmd.arguments() << "terminated" << cmd.terminateAndKill();
    }
    Cmd().run(elevate + "/usr/lib/mx-packageinstaller/mxpi-lib copy_log", Cmd::QuietMode::Yes);
    settings.setValue("geometry", saveGeometry());
    settings.setValue("FlatpakRemote", ui->comboRemote->currentText());
    settings.setValue("FlatpakUser", ui->comboUser->currentText());
}

QString MainWindow::getVersion(const QString &name) const
{
    const QString output = Cmd().getOut("LANG=C pacman -Q " + name);
    return output.section(' ', 1).trimmed();
}

// Return true if all the packages listed are installed
bool MainWindow::checkInstalled(const QVariant &names) const
{

    QStringList name_list;
    if (names.canConvert<QStringList>()) {
        name_list = names.toStringList();
        // Flatten any strings in the list that contain newlines
        QStringList expanded_list;
        for (const QString &name : name_list) {
            if (name.contains('\n')) {
                expanded_list.append(name.split('\n', Qt::SkipEmptyParts));
            } else {
                expanded_list.append(name);
            }
        }
        name_list = expanded_list;
    } else if (names.canConvert<QString>()) {
        name_list = names.toString().split('\n', Qt::SkipEmptyParts);
    } else {
        return false;
    }

    if (name_list.isEmpty()) {
        return false;
    }

    if (installedPackages.isEmpty()) {
        Cmd shell;
        for (const QString &name : name_list) {
            if (name.trimmed().isEmpty()) {
                continue;
            }
            const QString out = shell.getOut("pacman -Qq --color never " + shellQuote(name), Cmd::QuietMode::Yes);
            Q_UNUSED(out);
            if (shell.exitStatus() != QProcess::NormalExit || shell.exitCode() != 0) {
                return false;
            }
        }
        return true;
    }

    // Trim whitespace from all package names
    for (QString &name : name_list) {
        name = name.trimmed();
    }
    for (const QString &name : name_list) {
        if (!installedPackages.contains(name)) {
            return false;
        }
    }
    return true;
}

// Return true if all the items in the list are upgradable
bool MainWindow::checkUpgradable(const QStringList &name_list) const
{
    qDebug() << "+++" << __PRETTY_FUNCTION__ << "+++";
    if (name_list.isEmpty()) {
        return false;
    }
    for (const QString &name : name_list) {
        auto item_list = currentTree->findItems(name, Qt::MatchExactly, TreeCol::Name);
        if (item_list.isEmpty() || item_list.at(0)->data(TreeCol::Status, Qt::UserRole) != Status::Upgradable) {
            return false;
        }
    }
    return true;
}

QHash<QString, PackageInfo> MainWindow::listInstalled()
{
    qDebug() << "+++" << __PRETTY_FUNCTION__ << "+++";
    Cmd shell;
    const QString list = shell.getOut("LANG=C pacman -Qi");

    if (shell.exitStatus() != QProcess::NormalExit || shell.exitCode() != 0) {
        QMessageBox::critical(this, tr("Error"),
                              tr("pacman query returned an error. Please run 'pacman -Qi' in terminal "
                                 "and check the output."));
        // Return empty hash instead of calling exit() - allows graceful degradation
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

QStringList MainWindow::listFlatpaks(const QString &remote, const QString &type) const
{
    qDebug() << "+++" << __PRETTY_FUNCTION__ << "+++";

    QString arch_fp = getArchOption();
    if (arch_fp.isEmpty()) {
        return {};
    }

    // Check if remote parameter is empty (which would happen if no remotes are configured)
    if (remote.isEmpty()) {
        qDebug() << "Remote parameter is empty - no flatpak remotes configured for user";
        return {};
    }

    const bool isUserScope = fpUser.startsWith("--user");

    auto buildRemoteLsCommand = [&](const QString &scope) {
        return "flatpak remote-ls " + scope + remote + ' ' + arch_fp + "--columns=ver,branch,ref,installed-size ";
    };

    QString typeFlag;
    if (type == QLatin1String("--app")) {
        typeFlag = "--app ";
    } else if (type == QLatin1String("--runtime")) {
        typeFlag = "--runtime ";
    }
    const QString commandSuffix = typeFlag + "2>/dev/null";

    // Construct the base command for listing flatpaks
    QString baseCommand = buildRemoteLsCommand(fpUser) + commandSuffix;

    auto runRemoteLs = [](const QString &command) {
        Cmd shell;
        QStringList output;
        if (shell.run(command)) {
            output = shell.readAllOutput().split('\n', Qt::SkipEmptyParts);
        }
        return output;
    };

    // Execute the command and process the output
    QStringList list = runRemoteLs(baseCommand);

    if (list.isEmpty()) {
        qDebug() << QString("Could not list packages from %1 remote, attempting to update remote").arg(remote);

        // Try to update the remote if it's empty
        QString updateCommand = "flatpak update " + fpUser + "--appstream " + remote + " 2>/dev/null";
        qDebug() << "Running remote update command:" << updateCommand;

        Cmd updateShell;
        if (updateShell.run(updateCommand)) {
            qDebug() << "Remote update completed, retrying package list";

            // Retry the original command after update
            list = runRemoteLs(baseCommand);
            if (!list.isEmpty()) {
                qDebug() << QString("Successfully retrieved %1 packages after remote update").arg(list.size());
            } else {
                qDebug() << QString("Remote %1 still empty after update").arg(remote);
            }
        } else {
            qDebug() << "Failed to update remote" << remote;
        }
    }

    // If user scope returned nothing (e.g. only system remotes exist), fall back to system remotes for listing
    if (list.isEmpty() && isUserScope) {
        const QString systemCommand = buildRemoteLsCommand(QStringLiteral("--system ")) + commandSuffix;
        qDebug() << "User remotes empty; retrying flatpak listing using system remotes:" << systemCommand;
        list = runRemoteLs(systemCommand);
    }

    return list;
}

// List installed flatpaks by type: apps, runtimes, or all (if no type is provided)
QStringList MainWindow::listInstalledFlatpaks(const QString &type)
{
    QStringList lines;
    if (cachedInstalledScope == fpUser && cachedInstalledFetched) {
        lines = cachedInstalledFlatpaks;
    } else {
        const QString command = "flatpak list " + fpUser + "2>/dev/null " + type + " --columns=ref";
        QScopedValueRollback<bool> guard(suppressCmdOutput, true);
        lines = cmd.getOut(command, Cmd::QuietMode::No).split('\n', Qt::SkipEmptyParts);

        if (type.isEmpty()) {
            cachedInstalledFlatpaks = lines;
            cachedInstalledScope = fpUser;
            cachedInstalledFetched = true;
        }
    }

    QStringList refs;
    for (const QString &lineRaw : lines) {
        if (lineRaw.startsWith("Ref")) { // skip header if present
            continue;
        }
        const QString line = lineRaw.section('\t', 0, 0);

        const ParsedFlatpakRef parsed = parseInstalledFlatpakLine(line);
        if (parsed.ref.isEmpty()) {
            continue;
        }

        if (type == QLatin1String("--app") && parsed.isRuntime) {
            continue;
        }
        if (type == QLatin1String("--runtime") && !parsed.isRuntime) {
            continue;
        }

        refs.append(parsed.ref);
    }
    return refs;
}

QTreeWidgetItem *MainWindow::createTreeItem(const QString &name, const QString &version,
                                            const QString &description) const
{
    auto *widget_item = new QTreeWidgetItem();
    widget_item->setCheckState(TreeCol::Check, Qt::Unchecked);
    widget_item->setText(TreeCol::Name, name);
    widget_item->setText(TreeCol::RepoVersion, version);
    widget_item->setText(TreeCol::Description, description);
    widget_item->setData(0, Qt::UserRole, true); // All items are displayed till filtered
    return widget_item;
}

void MainWindow::setCurrentTree()
{
    qDebug() << "+++" << __PRETTY_FUNCTION__ << "+++";
    const QList<QTreeWidget *> trees = {ui->treeRepo, ui->treeAUR, ui->treeFlatpak};

    auto it = std::find_if(trees.cbegin(), trees.cend(), [](const QTreeWidget *tree) { return tree->isVisible(); });

    if (it != trees.cend()) {
        currentTree = *it;
    }
}

void MainWindow::setDirty()
{
    dirtyAur = true;
    dirtyRepo = true;
    repoCacheValid = false;
    aurInstalledCacheValid = false;
    aurInstalledCache.clear();
    ++aurInstalledCacheEpoch;
    aurUpgradesLoading = false;
    installedVersionsCache.clear();
    installedVersionsCacheTimer.invalidate();
    cachedAutoremovableFetched = false;
}

void MainWindow::setIcons()
{

    const QString icon_upgradable {"package-installed-outdated"};
    const QString icon_installed {"package-installed-updated"};

    const QIcon backup_icon_upgradable(":/icons/package-installed-outdated.png");
    const QIcon backup_icon_installed(":/icons/package-installed-updated.png");

    const QIcon theme_icon_upgradable = QIcon::fromTheme(icon_upgradable, backup_icon_upgradable);
    const QIcon theme_icon_installed = QIcon::fromTheme(icon_installed, backup_icon_installed);

    const bool force_backup_icon = (theme_icon_upgradable.name() == theme_icon_installed.name());

    qiconInstalled = force_backup_icon ? backup_icon_installed : theme_icon_installed;
    qiconUpgradable = force_backup_icon ? backup_icon_upgradable : theme_icon_upgradable;
    const auto upgradableIcons = {ui->iconUpgradableRepo, ui->iconUpgradable_2};
    const auto installedIcons = {ui->iconInstalledPackagesRepo, ui->iconInstalledPackages_2,
                                 ui->iconInstalledPackages_4};
    for (auto *icon : upgradableIcons) {
        icon->setIcon(qiconUpgradable);
    }
    for (auto *icon : installedIcons) {
        icon->setIcon(qiconInstalled);
    }
}

QHash<QString, VersionNumber> MainWindow::listInstalledVersions()
{
    if (!installedVersionsCache.isEmpty() && installedVersionsCacheTimer.isValid()
        && installedVersionsCacheTimer.elapsed() < 1500) {
        return installedVersionsCache;
    }
    qDebug() << "+++" << __PRETTY_FUNCTION__ << "+++";
    QHash<QString, VersionNumber> installedVersions;
    Cmd shell;
    const QString command = "LANG=C pacman -Q";
    const QStringList packageList = shell.getOut(command, Cmd::QuietMode::Yes).split('\n', Qt::SkipEmptyParts);

    if (shell.exitStatus() != QProcess::NormalExit || shell.exitCode() != 0) {
        QMessageBox::critical(
            this, tr("Error"),
            tr("pacman query returned an error, please run 'pacman -Q' in terminal and check the output."));
        return installedVersions;
    }
    for (const QString &line : packageList) {
        const QStringList packageInfo = line.split(' ', Qt::SkipEmptyParts);
        if (packageInfo.size() >= 2) {
            installedVersions.insert(packageInfo.at(0), VersionNumber(packageInfo.at(1)));
        }
    }
    installedVersionsCache = installedVersions;
    installedVersionsCacheTimer.start();
    return installedVersions;
}

void MainWindow::cmdStart()
{
    if (!timer.isActive()) {
        timer.start(100ms);
    }
    setCursor(QCursor(Qt::BusyCursor));
    ui->lineEdit->setFocus();
}

void MainWindow::cmdDone()
{
    timer.stop();
    setCursor(QCursor(Qt::ArrowCursor));
    disableOutput();
    if (!holdProgressForFlatpakRefresh && !holdProgressForRepoRefresh) {
        progress->hide();
    }
}

void MainWindow::enableOutput()
{
    connect(&cmd, &Cmd::outputAvailable, this, &MainWindow::outputAvailable, Qt::UniqueConnection);
    connect(&cmd, &Cmd::errorAvailable, this, &MainWindow::outputAvailable, Qt::UniqueConnection);
}

void MainWindow::disableOutput()
{
    disconnect(&cmd, &Cmd::outputAvailable, this, &MainWindow::outputAvailable);
    disconnect(&cmd, &Cmd::errorAvailable, this, &MainWindow::outputAvailable);
}

void MainWindow::displayPackageInfo(const QTreeWidget *tree, QPoint pos)
{
    auto *t_widget = qobject_cast<QTreeWidget *>(focusWidget());
    if (!t_widget) {
        qWarning() << "No tree widget in focus";
        return;
    }

    auto *action = new QAction(QIcon::fromTheme("dialog-information"), tr("More &info..."), this);
    QMenu menu(this);
    menu.addAction(action);
    connect(action, &QAction::triggered, this, [this, t_widget] { displayPackageInfo(t_widget->currentItem()); });
    menu.exec(t_widget->mapToGlobal(pos));
}

void MainWindow::displayPackageInfo(const QTreeWidgetItem *item)
{
    const QTreeWidget *tree = item ? item->treeWidget() : nullptr;
    const bool isAurTree = tree == ui->treeAUR;
    QString baseCommand;
    if (isAurTree) {
        const QString paruPath = getParuPath();
        if (paruPath.isEmpty()) {
            QMessageBox::critical(this, tr("Error"), tr("paru is not installed."));
            return;
        }
        baseCommand = paruPath + " -Si --color never ";
    } else {
        baseCommand = "pacman -Si --color never ";
    }
    const QString packageName = item->text(TreeCol::Name);
    auto isErrorOutput = [](const QString &text) {
        const QString trimmed = text.trimmed();
        if (trimmed.isEmpty()) {
            return true;
        }
        const QString lower = trimmed.toLower();
        return lower.startsWith("error:") || lower.contains("was not found") || lower.contains("target not found")
               || lower.contains("could not find");
    };

    QString msg;
    if (isAurTree && installedPackages.contains(packageName)) {
        msg = cmd.getOut("pacman -Qi --color never " + packageName);
    }
    if (isErrorOutput(msg)) {
        msg = cmd.getOut(baseCommand + packageName);
    }
    if (isErrorOutput(msg) && isAurTree) {
        msg = cmd.getOut(baseCommand.replace(" -Si ", " -Qi ") + packageName);
    }
    if (isErrorOutput(msg)) {
        msg = cmd.getOut("pacman -Qi --color never " + packageName);
    }

    QMessageBox info(QMessageBox::NoIcon, tr("Package info"), msg.trimmed(), QMessageBox::Close);

    // Make it wider
    auto *horizontalSpacer = new QSpacerItem(width(), 0, QSizePolicy::Minimum, QSizePolicy::Expanding);
    auto *layout = qobject_cast<QGridLayout *>(info.layout());
    layout->addItem(horizontalSpacer, 0, 1);
    info.exec();
}

void MainWindow::findPackage()
{
    // Get search text from appropriate search box
    const QMap<QTreeWidget *, QLineEdit *> searchBoxMap = {{ui->treeRepo, ui->searchBoxRepo},
                                                           {ui->treeAUR, ui->searchBoxAUR},
                                                           {ui->treeFlatpak, ui->searchBoxFlatpak}};

    QLineEdit *searchBox = searchBoxMap.value(currentTree, nullptr);
    if (!searchBox) {
        return;
    }
    const QString word = searchBox->text();

    // Skip single character searches
    if (word.length() == 1) {
        return;
    }

    if (currentTree == ui->treeRepo) {
        // Repo search is local-only; filter the already displayed list.
    } else if (currentTree == ui->treeAUR) {
        const QString filterText = ui->comboFilterAUR->currentText();
        const QString trimmed = word.trimmed();

        if (!trimmed.isEmpty() && trimmed.size() < 2) {
            return;
        }

        if (trimmed.isEmpty()) {
            if (filterText == tr("All packages")) {
                buildAurList(trimmed);
                dirtyAur = true;
                displayPackages();
            } else if (filterText == tr("Not installed")) {
                aurList.clear();
                dirtyAur = true;
                displayPackages();
                updateInterface();
                return;
            }
            // Fall through to local filtering for Installed/Upgradable/Autoremovable.
        } else if (filterText == tr("All packages") || filterText == tr("Not installed")) {
            buildAurList(trimmed);
            dirtyAur = true;
            displayPackages();

            if (filterText == tr("Not installed")) {
                const QStringList aurInstalledLines
                    = cmd.getOut("pacman -Qm --color never").split('\n', Qt::SkipEmptyParts);
                QSet<QString> aurInstalled;
                aurInstalled.reserve(aurInstalledLines.size());
                for (const QString &line : aurInstalledLines) {
                    const QString name = line.section(' ', 0, 0).trimmed();
                    if (!name.isEmpty()) {
                        aurInstalled.insert(name);
                    }
                }
                for (QTreeWidgetItemIterator it(currentTree); (*it) != nullptr; ++it) {
                    const QString name = (*it)->text(TreeCol::Name);
                    const bool shouldShow = !aurInstalled.contains(name);
                    (*it)->setHidden(!shouldShow);
                    (*it)->setData(0, Qt::UserRole, shouldShow);
                }
            }
        }
        // Continue to local filtering for Installed/Upgradable/Autoremovable and for search refinement.
    }

    currentTree->setUpdatesEnabled(false);

    // Track matching items and their ancestors
    QSet<QTreeWidgetItem *> foundItems;

    // Search appropriate columns based on tree type
    QVector<int> searchColumns;
    if (currentTree == ui->treeFlatpak) {
        searchColumns = {FlatCol::LongName};
    } else {
        searchColumns = {TreeCol::Name, TreeCol::Description};
    }

    // Find matches in each column
    for (int column : searchColumns) {
        // Check if the search term contains wildcards (* or ?)
        QRegularExpression regExp;
        if (word.contains('*') || word.contains('?')) {
            // Convert the glob pattern to a regular expression
            QString pattern = QRegularExpression::escape(word);
            pattern.replace("\\*", ".*");
            pattern.replace("\\?", ".");
            regExp = QRegularExpression(pattern, QRegularExpression::CaseInsensitiveOption);
        } else {
            // Use standard search for non-wildcard searches
            regExp = QRegularExpression(QRegularExpression::escape(word), QRegularExpression::CaseInsensitiveOption);
        }

        // Check each item against the regex pattern
        for (QTreeWidgetItemIterator it(currentTree); *it; ++it) {
            QTreeWidgetItem *item = *it;
            if (regExp.match(item->text(column)).hasMatch()) {
                // Add match and its ancestors to found set
                QTreeWidgetItem *ancestor = item;
                while (ancestor) {
                    foundItems.insert(ancestor);
                    ancestor = ancestor->parent();
                }
            }
        }
    }

    // Show/hide items based on search results
    for (QTreeWidgetItemIterator it(currentTree); *it; ++it) {
        QTreeWidgetItem *item = *it;
        const bool isHidden = item->data(0, Qt::UserRole) == false;
        item->setHidden(!foundItems.contains(item) || isHidden);
    }

    currentTree->setUpdatesEnabled(true);
    updateInterface();
}

void MainWindow::onAurSearchTextChanged()
{
    // Reset search when AUR search box is cleared
    if (ui->searchBoxAUR->text().trimmed().isEmpty()) {
        findPackage();
    }
}

void MainWindow::showOutput()
{
    ui->outputBox->clear();
    ui->tabWidget->setTabEnabled(Tab::Output, true);
    ui->tabWidget->setCurrentWidget(ui->tabOutput);
    enableTabs(false);
}

void MainWindow::pushInstall_clicked()
{
    qDebug() << "+++" << __PRETTY_FUNCTION__ << "+++";
    showOutput();
    if (currentTree == ui->treeFlatpak) {
        // Confirmation dialog
        if (!confirmActions(changeList.join(' '), "install")) {
            displayFlatpaks(true);
            indexFilterFP.clear();
            ui->comboFilterFlatpak->setCurrentIndex(0);
            QMessageBox::information(this, tr("Done"), tr("Processing finished successfully."));
            ui->tabWidget->setCurrentWidget(ui->tabFlatpak);
            enableTabs(true);
            return;
        }
        setCursor(QCursor(Qt::BusyCursor));
        enableOutput();
        QStringList quotedPackages;
        quotedPackages.reserve(changeList.size());
        for (const QString &pkg : std::as_const(changeList)) {
            quotedPackages.append(shellQuote(pkg));
        }
        if (cmd.run("socat SYSTEM:'flatpak install -y " + fpUser + shellQuote(ui->comboRemote->currentText()) + ' '
                    + quotedPackages.join(' ') + "',stderr STDIO")) {
            displayFlatpaks(true);
            indexFilterFP.clear();
            ui->comboFilterFlatpak->setCurrentIndex(0);
            QMessageBox::information(this, tr("Done"), tr("Processing finished successfully."));
            ui->tabWidget->setCurrentWidget(ui->tabFlatpak);
        } else {
            setCursor(QCursor(Qt::ArrowCursor));
            QMessageBox::critical(this, tr("Error"),
                                  tr("Problem detected while installing, please inspect the console output."));
        }
    } else {
        bool success = false;
        if ((currentTree == ui->treeRepo && ui->comboFilterRepo->currentText() == tr("Autoremovable"))
            || (currentTree == ui->treeAUR && ui->comboFilterAUR->currentText() == tr("Autoremovable"))) {
            success = markKeep();
        } else {
            success = installSelected();
        }
        setDirty();
        buildPackageLists();
        if (success) {
            QMessageBox::information(this, tr("Done"), tr("Processing finished successfully."));
            ui->tabWidget->setCurrentWidget(currentTree->parentWidget());
        } else {
            QMessageBox::critical(this, tr("Error"),
                                  tr("Problem detected while installing, please inspect the console output."));
        }
    }
    enableTabs(true);
}

void MainWindow::pushAbout_clicked()
{
    hide();
    displayAboutMsgBox(
        tr("About %1").arg(windowTitle()),
        "<p align=\"center\"><b><h2>" + windowTitle() + "</h2></b></p><p align=\"center\">" + tr("Version: ")
            + QCoreApplication::applicationVersion() + "</p><p align=\"center\"><h3>"
            + tr("Package Installer for Arch Linux")
            + R"(</h3></p><p align="center"><a href="http://mxlinux.org">http://mxlinux.org</a><br /></p><p align="center">)"
            + tr("Copyright (c) MX Linux") + "<br /><br /></p>",
        "/usr/share/doc/mx-packageinstaller/license.html", tr("%1 License").arg(windowTitle()));
    show();
}

void MainWindow::pushHelp_clicked()
{
    QString lang = locale.bcp47Name();
    QString url {"/usr/share/doc/mx-packageinstaller/mx-package-installer.html"};

    if (lang.startsWith("fr")) {
        url = "https://mxlinux.org/wiki/help-files/help-mx-installateur-de-paquets";
    }
    displayDoc(url, tr("%1 Help").arg(windowTitle()));
}

void MainWindow::pushUninstall_clicked()
{
    qDebug() << "+++" << __PRETTY_FUNCTION__ << "+++";

    showOutput();

    QString names;
    QString preuninstall;
    QString postuninstall;
    if (currentTree == ui->treeFlatpak) {
        bool success = true;

        // Confirmation dialog
        if (!confirmActions(changeList.join(' '), "remove")) {
            displayFlatpaks(true);
            indexFilterFP.clear();
            listFlatpakRemotes();
            ui->comboRemote->setCurrentIndex(0);
            comboRemote_activated();
            ui->comboFilterFlatpak->setCurrentIndex(0);
            QMessageBox::information(this, tr("Done"), tr("Processing finished successfully."));
            ui->tabWidget->setCurrentWidget(ui->tabFlatpak);
            enableTabs(true);
            return;
        }

        setCursor(QCursor(Qt::BusyCursor));
        for (const QString &app : std::as_const(changeList)) {
            enableOutput();
            if (!cmd.run("socat SYSTEM:'flatpak uninstall " + fpUser + "-y " + shellQuote(app)
                         + "',stderr STDIO")) { // success if all processed successfuly,
                                                // failure if one failed
                success = false;
            }
        }
        if (success) { // Success if all processed successfuly, failure if one failed
            displayFlatpaks(true);
            indexFilterFP.clear();
            listFlatpakRemotes();
            ui->comboRemote->setCurrentIndex(0);
            comboRemote_activated();
            ui->comboFilterFlatpak->setCurrentIndex(0);
            QMessageBox::information(this, tr("Done"), tr("Processing finished successfully."));
            ui->tabWidget->setCurrentWidget(ui->tabFlatpak);
        } else {
            QMessageBox::critical(this, tr("Error"), tr("We encountered a problem uninstalling, please check output"));
        }
        enableTabs(true);
        return;
    } else {
        names = changeList.join(' ');
    }

    bool success = uninstall(names, preuninstall, postuninstall);
    setDirty();
    buildPackageLists();
    if (success) {
        QMessageBox::information(this, tr("Success"), tr("Processing finished successfully."));
        ui->tabWidget->setCurrentWidget(currentTree->parentWidget());
    } else {
        QMessageBox::critical(this, tr("Error"), tr("We encountered a problem uninstalling the program"));
    }
    enableTabs(true);
}

void MainWindow::tabWidget_currentChanged(int index)
{
    qDebug() << "+++" << __PRETTY_FUNCTION__ << "+++";
    ui->tabWidget->setTabText(Tab::Output, tr("Console Output"));
    ui->pushInstall->setEnabled(false);
    ui->pushUninstall->setEnabled(false);

    resetCheckboxes();
    QString search_str;
    saveSearchText(search_str, savedComboIndex);
    if (index != Tab::Output) {
        setCurrentTree();
    }
    currentTree->blockSignals(true);
    auto setTabsEnabled = [this](bool enable) {
        for (auto tab : {Tab::Repos, Tab::AUR, Tab::Flatpak}) {
            if (tab != ui->tabWidget->currentIndex()) {
                ui->tabWidget->setTabEnabled(tab, enable);
            }
        }
    };
    setTabsEnabled(false);
    switch (index) {
    case Tab::Repos:
        handleRepoTab(search_str);
        break;
    case Tab::AUR:
        handleAurTab(search_str);
        break;
    case Tab::Flatpak:
        handleFlatpakTab(search_str);
        break;
    case Tab::Output:
        handleOutputTab();
        break;
    }
    setTabsEnabled(true);
}

void MainWindow::resetCheckboxes()
{
    currentTree->blockSignals(true);
    currentTree->clearSelection();
    for (QTreeWidgetItemIterator it(currentTree); (*it) != nullptr; ++it) {
        (*it)->setCheckState(TreeCol::Check, Qt::Unchecked);
    }
}

void MainWindow::saveSearchText(QString &search_str, int &filter_idx)
{
    if (currentTree == ui->treeRepo) {
        search_str = ui->searchBoxRepo->text();
        filter_idx = ui->comboFilterRepo->currentIndex();
    } else if (currentTree == ui->treeAUR) {
        search_str = ui->searchBoxAUR->text();
        filter_idx = ui->comboFilterAUR->currentIndex();
    } else if (currentTree == ui->treeFlatpak) {
        search_str = ui->searchBoxFlatpak->text();
    }
}

void MainWindow::handleAurTab(const QString &search_str)
{
    if (getParuPath().isEmpty()) {
        QMessageBox::critical(this, tr("Error"),
                              tr("Could not query AUR packages. Please check that paru is installed."));
        currentTree->blockSignals(false);
        return;
    }
    // Block signals to prevent onAurSearchTextChanged from triggering during setText
    ui->searchBoxAUR->blockSignals(true);
    ui->searchBoxAUR->setText(search_str);
    ui->searchBoxAUR->blockSignals(false);

    changeList.clear();
    displayWarning("aur");

    const bool needsReload = dirtyAur || ui->treeAUR->topLevelItemCount() == 0;
    if (needsReload) {
        // Let filterChanged handle the buildAurList, displayPackages, and applyAurStatusFilter calls
        // to avoid redundant command executions
        filterChanged(ui->comboFilterAUR->currentText());
    } else {
        if (ui->comboFilterAUR->currentText() == tr("All packages")
            && ui->searchBoxAUR->text().trimmed().isEmpty()) {
            startAurUpgradesLoad();
        }
        QMetaObject::invokeMethod(this, [this] { findPackage(); }, Qt::QueuedConnection);
        setSearchFocus();
        updateInterface();
    }
    currentTree->blockSignals(false);
}

void MainWindow::handleRepoTab(const QString &search_str)
{
    // Block signals to prevent onRepoSearchTextChanged from triggering during setText
    ui->searchBoxRepo->blockSignals(true);
    ui->searchBoxRepo->setText(search_str);
    ui->searchBoxRepo->blockSignals(false);

    changeList.clear();
    filterChanged(ui->comboFilterRepo->currentText());
    currentTree->blockSignals(false);
}


QString MainWindow::getParuPath()
{
    if (cachedParuPathFetched) {
        return cachedParuPath;
    }

    cachedParuPath = QStandardPaths::findExecutable("paru");
    if (cachedParuPath.isEmpty()) {
        const QStringList fallbackPaths = {"/usr/bin/paru", "/bin/paru", "/usr/local/bin/paru"};
        for (const QString &path : fallbackPaths) {
            if (QFile::exists(path)) {
                cachedParuPath = path;
                break;
            }
        }
    }
    cachedParuPathFetched = true;
    return cachedParuPath;
}

bool MainWindow::buildAurList(const QString &searchTerm)
{
    aurList.clear();
    const QString term = searchTerm.trimmed();
    const QString paruPath = getParuPath();
    if (paruPath.isEmpty()) {
        return false;
    }

    Cmd shell;
    QScopedValueRollback<bool> guard(suppressCmdOutput, true);
    if (term.isEmpty()) {
        if (aurInstalledCacheValid) {
            aurList = aurInstalledCache;
            startAurUpgradesLoad();
            return true;
        }
        const QStringList installed
            = shell.getOut("pacman -Qm --color never").split('\n', Qt::SkipEmptyParts);
        if (shell.exitStatus() != QProcess::NormalExit || shell.exitCode() != 0) {
            return false;
        }
        if (installed.isEmpty()) {
            return true;
        }

        if (installedPackages.isEmpty()) {
            installedPackages = listInstalled();
        }

        QHash<QString, QString> installedVersions;
        installedVersions.reserve(installed.size());
        for (const QString &line : installed) {
            const QStringList parts = line.split(' ', Qt::SkipEmptyParts);
            if (parts.size() >= 2) {
                installedVersions.insert(parts.at(0), parts.at(1));
            }
        }

        QHash<QString, QString> aurUpdates;
        const QStringList updates
            = shell.getOut(paruPath + " -Qua --color never").split('\n', Qt::SkipEmptyParts);
        if (shell.exitStatus() == QProcess::NormalExit && shell.exitCode() == 0) {
            aurUpdates.reserve(updates.size());
            for (const QString &line : updates) {
                const QString name = line.section(' ', 0, 0).trimmed();
                const QString newVersion = line.section("->", 1).trimmed();
                if (!name.isEmpty() && !newVersion.isEmpty()) {
                    aurUpdates.insert(name, newVersion);
                }
            }
        }

        for (auto it = installedVersions.cbegin(); it != installedVersions.cend(); ++it) {
            const QString repoVersion = aurUpdates.value(it.key(), it.value());
            const QString description = installedPackages.value(it.key()).description;
            aurList.insert(it.key(), {repoVersion, description});
        }
        aurInstalledCache = aurList;
        aurInstalledCacheValid = true;
        return true;
    }

    if (!isOnline()) {
        return false;
    }
    auto shellQuote = [](const QString &value) {
        QString escaped = value;
        escaped.replace('\'', QLatin1String("'\\''"));
        return '\'' + escaped + '\'';
    };
    QStringList results
        = shell.getOut(paruPath + " -Ssq --color never " + shellQuote(term)).split('\n', Qt::SkipEmptyParts);
    if (shell.exitStatus() != QProcess::NormalExit || shell.exitCode() != 0) {
        return false;
    }
    if (results.isEmpty()) {
        return true;
    }

    constexpr int maxResults = 200;
    if (results.size() > maxResults) {
        results = results.mid(0, maxResults);
    }

    const QString infoOutput = shell.getOut(paruPath + " -Si --color never " + results.join(' '));
    if (shell.exitStatus() != QProcess::NormalExit || shell.exitCode() != 0) {
        for (const QString &name : std::as_const(results)) {
            aurList.insert(name, {QString(), QString()});
        }
        return true;
    }
    if (infoOutput.trimmed().isEmpty()) {
        for (const QString &name : std::as_const(results)) {
            aurList.insert(name, {QString(), QString()});
        }
        return true;
    }

    QString packageName;
    QString version;
    QString description;
    auto flushPackage = [&]() {
        if (!packageName.isEmpty()) {
            aurList.insert(packageName, {version, description});
        }
        packageName.clear();
        version.clear();
        description.clear();
    };

    const QStringList lines = infoOutput.split('\n');
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
    return true;
}

bool MainWindow::buildRepoCache(bool showProgress)
{
    repoAllList.clear();
    repoInstalledSet.clear();
    repoUpgradableSet.clear();
    repoAutoremovableSet.clear();

    const bool shouldShowProgress = showProgress && ui->tabWidget->currentIndex() == Tab::Repos && !progress->isVisible();
    if (shouldShowProgress) {
        progress->show();
        if (!timer.isActive()) {
            timer.start(100ms);
        }
    }
    QApplication::setOverrideCursor(Qt::WaitCursor);

    auto finish = [&](bool result) {
        QApplication::restoreOverrideCursor();
        if (shouldShowProgress && !displayFlatpaksIsRunning) {
            progress->hide();
            if (cmd.state() == QProcess::NotRunning) {
                timer.stop();
            }
        }
        return result;
    };

    Cmd shell;
    QScopedValueRollback<bool> guard(suppressCmdOutput, true);

    const QString infoOutput = shell.getOut("pacman -Ss --color never");
    if (shell.exitStatus() != QProcess::NormalExit || shell.exitCode() != 0) {
        return finish(false);
    }

    struct RepoEntry {
        QString name;
        QString version;
        QString description;
    };

    QRegularExpression headerRegex(R"(^\S+/(\S+)\s+(\S+))");
    QString currentName;
    QString currentVersion;

    auto flushEntry = [&](const QString &desc) {
        if (!currentName.isEmpty()) {
            repoAllList.insert(currentName, {currentVersion, desc});
        }
        currentName.clear();
        currentVersion.clear();
    };

    QString pendingDescription;
    const QStringList lines = infoOutput.split('\n');
    for (const QString &line : lines) {
        if (line.startsWith(' ') || line.startsWith('\t')) {
            if (!currentName.isEmpty()) {
                pendingDescription = line.trimmed();
                flushEntry(pendingDescription);
                pendingDescription.clear();
            }
            continue;
        }
        if (!currentName.isEmpty()) {
            flushEntry(pendingDescription);
            pendingDescription.clear();
        }
        const QRegularExpressionMatch match = headerRegex.match(line);
        if (!match.hasMatch()) {
            continue;
        }
        currentName = match.captured(1);
        currentVersion = match.captured(2);
    }
    if (!currentName.isEmpty()) {
        flushEntry(pendingDescription);
    }

    if (!installedPackages.isEmpty()) {
        updateRepoSetsFromInstalled();
    }

    repoCacheValid = true;
    return finish(true);
}

void MainWindow::applyRepoFilter(int statusFilter)
{
    repoList.clear();
    if (!repoCacheValid) {
        return;
    }

    if (statusFilter == Status::Autoremovable && repoAutoremovableSet.isEmpty()) {
        const QStringList autoremovable = getAutoremovablePackages();
        repoAutoremovableSet = QSet<QString>(autoremovable.cbegin(), autoremovable.cend());
    }

    auto ensureRepoInstalledSet = [&]() {
        if (!repoInstalledSet.isEmpty()) {
            return;
        }
        if (!installedPackages.isEmpty()) {
            updateRepoSetsFromInstalled();
            return;
        }
        Cmd shell;
        QScopedValueRollback<bool> guard(suppressCmdOutput, true);
        const QStringList lines = shell.getOut("pacman -Qq").split('\n', Qt::SkipEmptyParts);
        if (shell.exitStatus() != QProcess::NormalExit || shell.exitCode() != 0) {
            return;
        }
        repoInstalledSet.reserve(lines.size());
        for (const QString &line : lines) {
            const QString name = line.trimmed();
            if (!name.isEmpty()) {
                repoInstalledSet.insert(name);
            }
        }
    };

    auto ensureRepoUpgradableSet = [&]() {
        if (!repoUpgradableSet.isEmpty()) {
            return;
        }
        if (!installedPackages.isEmpty()) {
            updateRepoSetsFromInstalled();
            if (!repoUpgradableSet.isEmpty()) {
                return;
            }
        }
        Cmd shell;
        QScopedValueRollback<bool> guard(suppressCmdOutput, true);
        const QStringList lines = shell.getOut("pacman -Qu --color never").split('\n', Qt::SkipEmptyParts);
        if (shell.exitStatus() != QProcess::NormalExit || shell.exitCode() != 0) {
            return;
        }
        repoUpgradableSet.reserve(lines.size());
        for (const QString &line : lines) {
            const QString name = line.section(' ', 0, 0).trimmed();
            if (!name.isEmpty()) {
                repoUpgradableSet.insert(name);
            }
        }
    };

    if (statusFilter == Status::Installed) {
        ensureRepoInstalledSet();
    } else if (statusFilter == Status::Upgradable) {
        ensureRepoInstalledSet();
        ensureRepoUpgradableSet();
    }

    for (auto it = repoAllList.cbegin(); it != repoAllList.cend(); ++it) {
        bool shouldShow = true;
        if (statusFilter == Status::Installed) {
            shouldShow = repoInstalledSet.contains(it.key());
        } else if (statusFilter == Status::NotInstalled) {
            shouldShow = !repoInstalledSet.contains(it.key());
        } else if (statusFilter == Status::Upgradable) {
            shouldShow = repoUpgradableSet.contains(it.key());
        } else if (statusFilter == Status::Autoremovable) {
            shouldShow = repoAutoremovableSet.contains(it.key());
        }
        if (shouldShow) {
            repoList.insert(it.key(), it.value());
        }
    }

    displayPackages();

    if (statusFilter == Status::Autoremovable) {
        for (QTreeWidgetItemIterator it(ui->treeRepo); (*it) != nullptr; ++it) {
            if (!(*it)->isHidden()) {
                (*it)->setData(TreeCol::Status, Qt::UserRole, Status::Autoremovable);
            }
        }
    }

    if (currentTree == ui->treeRepo) {
        QMetaObject::invokeMethod(this, &MainWindow::updateInterface, Qt::QueuedConnection);
    }
}

void MainWindow::startInstalledPackagesLoad()
{
    if (installedPackagesLoading || !installedPackages.isEmpty()) {
        return;
    }
    installedPackagesLoading = true;
    installedPackagesWatcher.setFuture(QtConcurrent::run([this] { return listInstalled(); }));
}

void MainWindow::startAurInstalledCacheLoad()
{
    if (aurInstalledCacheLoading || aurInstalledCacheValid) {
        return;
    }
    const QString paruPath = getParuPath();
    if (paruPath.isEmpty()) {
        return;
    }

    aurInstalledCacheLoading = true;
    aurInstalledCacheEpochInFlight = aurInstalledCacheEpoch;
    aurInstalledCacheWatcher.setFuture(QtConcurrent::run([paruPath]() {
        QHash<QString, PackageInfo> result;
        Cmd shell;

        const QStringList installedLines
            = shell.getOut("pacman -Qm --color never", Cmd::QuietMode::Yes).split('\n', Qt::SkipEmptyParts);
        if (shell.exitStatus() != QProcess::NormalExit || shell.exitCode() != 0) {
            return result;
        }
        if (installedLines.isEmpty()) {
            return result;
        }

        QHash<QString, QString> installedVersions;
        installedVersions.reserve(installedLines.size());
        QSet<QString> installedNames;
        installedNames.reserve(installedLines.size());
        for (const QString &line : installedLines) {
            const QStringList parts = line.split(' ', Qt::SkipEmptyParts);
            if (parts.size() >= 2) {
                const QString name = parts.at(0);
                installedVersions.insert(name, parts.at(1));
                installedNames.insert(name);
            }
        }
        if (installedNames.isEmpty()) {
            return result;
        }

        QHash<QString, QString> descriptions;
        const QString infoOutput = shell.getOut("LANG=C pacman -Qi", Cmd::QuietMode::Yes);
        if (shell.exitStatus() == QProcess::NormalExit && shell.exitCode() == 0) {
            QString packageName;
            QString description;
            auto flushPackage = [&]() {
                if (!packageName.isEmpty() && installedNames.contains(packageName)) {
                    descriptions.insert(packageName, description);
                }
                packageName.clear();
                description.clear();
            };

            const QStringList lines = infoOutput.split('\n');
            for (const QString &line : lines) {
                if (line.startsWith("Name")) {
                    flushPackage();
                    packageName = line.section(':', 1).trimmed();
                } else if (line.startsWith("Description")) {
                    description = line.section(':', 1).trimmed();
                }
            }
            flushPackage();
        }

        for (auto it = installedVersions.cbegin(); it != installedVersions.cend(); ++it) {
            const QString repoVersion = it.value();
            const QString description = descriptions.value(it.key());
            result.insert(it.key(), {repoVersion, description});
        }
        return result;
    }));
}

void MainWindow::startAurUpgradesLoad()
{
    if (aurUpgradesLoading || !aurInstalledCacheValid) {
        return;
    }
    const QString paruPath = getParuPath();
    if (paruPath.isEmpty()) {
        return;
    }

    aurUpgradesLoading = true;
    aurUpgradesEpochInFlight = aurInstalledCacheEpoch;
    aurUpgradesWatcher.setFuture(QtConcurrent::run([paruPath]() {
        QHash<QString, QString> updates;
        Cmd shell;
        const QStringList lines
            = shell.getOut(paruPath + " -Qua --color never", Cmd::QuietMode::Yes).split('\n', Qt::SkipEmptyParts);
        if (shell.exitStatus() != QProcess::NormalExit || shell.exitCode() != 0) {
            return updates;
        }
        updates.reserve(lines.size());
        for (const QString &line : lines) {
            const QString name = line.section(' ', 0, 0).trimmed();
            const QString newVersion = line.section("->", 1).trimmed();
            if (!name.isEmpty() && !newVersion.isEmpty()) {
                updates.insert(name, newVersion);
            }
        }
        return updates;
    }));
}

void MainWindow::updateRepoSetsFromInstalled()
{
    repoInstalledSet.clear();
    repoUpgradableSet.clear();

    for (auto it = installedPackages.cbegin(); it != installedPackages.cend(); ++it) {
        repoInstalledSet.insert(it.key());
    }

    for (auto it = installedPackages.cbegin(); it != installedPackages.cend(); ++it) {
        const auto repoIt = repoAllList.constFind(it.key());
        if (repoIt == repoAllList.constEnd()) {
            continue;
        }
        const VersionNumber installedVersion(it.value().version);
        const VersionNumber repoVersion(repoIt.value().version);
        if (installedVersion < repoVersion) {
            repoUpgradableSet.insert(it.key());
        }
    }
}

void MainWindow::handleFlatpakTab(const QString &search_str)
{
    lastItemClicked = nullptr;
    ui->searchBoxFlatpak->setText(search_str);
    setCurrentTree();
    displayWarning("flatpaks");
    ui->searchBoxFlatpak->setFocus();
    listFlatpakRemotes();
    if (!firstRunFP && checkInstalled("flatpak")) {
        if (!search_str.isEmpty()) {
            QMetaObject::invokeMethod(this, [this] { findPackage(); }, Qt::QueuedConnection);
        }
        if (!displayFlatpaksIsRunning) {
            filterChanged(ui->comboFilterFlatpak->currentText());
        }
        currentTree->blockSignals(false);
        return;
    }
    firstRunFP = false;
    blockInterfaceFP(true);
    if (!checkInstalled("flatpak")) {
        int ans = QMessageBox::question(this, tr("Flatpak not installed"),
                                        tr("Flatpak is not currently installed.\nOK to go ahead and install it?"));
        if (ans == QMessageBox::No) {
            ui->tabWidget->setCurrentIndex(Tab::Repos);
            enableTabs(true);
            return;
        }
        installFlatpak();
    } else {
        setCursor(QCursor(Qt::BusyCursor));
        enableOutput();
        setCursor(QCursor(Qt::ArrowCursor));
        if (ui->comboRemote->currentText().isEmpty()) {
            listFlatpakRemotes();
        }
        if (displayFlatpaksIsRunning) {
            if (!flatpakCancelHidden && pushCancel) {
                pushCancel->setEnabled(false);
                pushCancel->hide();
                flatpakCancelHidden = true;
            }
            progress->show();
            if (!timer.isActive()) {
                timer.start(100ms);
                qApp->processEvents();
            }
        }
        if (!search_str.isEmpty()) {
            QMetaObject::invokeMethod(this, [this] { findPackage(); }, Qt::QueuedConnection);
        }
    }
}

void MainWindow::installFlatpak()
{
    ui->tabWidget->setTabEnabled(Tab::Output, true);
    ui->tabWidget->setCurrentWidget(ui->tabOutput);
    setCursor(QCursor(Qt::BusyCursor));
    showOutput();
    displayFlatpaksIsRunning = true;
    install("flatpak");
    installedPackages = listInstalled();
    setDirty();
    buildPackageLists();
    if (!checkInstalled("flatpak")) {
        QMessageBox::critical(this, tr("Flatpak not installed"), tr("Flatpak was not installed"));
        ui->tabWidget->setCurrentIndex(Tab::Repos);
        setCursor(QCursor(Qt::ArrowCursor));
        enableTabs(true);
        currentTree->blockSignals(false);
        return;
    }
    Cmd().run(elevate + "/usr/lib/mx-packageinstaller/mxpi-lib flatpak_add_repos", Cmd::QuietMode::Yes);
    enableOutput();
    invalidateFlatpakRemoteCache();
    listFlatpakRemotes();
    if (displayFlatpaksIsRunning) {
        progress->show();
        if (!timer.isActive()) {
            timer.start(100ms);
        }
    }
    setCursor(QCursor(Qt::ArrowCursor));
    ui->tabWidget->setTabText(Tab::Output, tr("Console Output"));
    ui->tabWidget->blockSignals(true);
    displayFlatpaks(true);
    ui->tabWidget->blockSignals(false);
    QMessageBox::warning(this, tr("Needs re-login"),
                         tr("You might need to logout/login to see installed items in the menu"));
    ui->tabWidget->setCurrentWidget(ui->tabFlatpak);
    enableTabs(true);
}

void MainWindow::handleOutputTab()
{
    // Block signals and clear all search boxes
    const QList<QLineEdit *> searchBoxes = {ui->searchBoxRepo, ui->searchBoxAUR, ui->searchBoxFlatpak};

    for (auto searchBox : searchBoxes) {
        searchBox->blockSignals(true);
        searchBox->clear();
        searchBox->blockSignals(false);
    }

    // Disable install/uninstall buttons
    ui->pushInstall->setDisabled(true);
    ui->pushUninstall->setDisabled(true);
}

void MainWindow::filterChanged(const QString &arg1)
{
    qDebug() << "+++" << __PRETTY_FUNCTION__ << "+++";
    currentTree->blockSignals(true);
    currentTree->setUpdatesEnabled(false);
    // Defer label updates until after filtering completes.

    // Helper functions
    auto resetTree = [this]() {
        // Optimization: Disable updates during bulk operations
        currentTree->setUpdatesEnabled(false);
        for (QTreeWidgetItemIterator it(currentTree); (*it) != nullptr; ++it) {
            (*it)->setData(0, Qt::UserRole, true);
            (*it)->setHidden(false);
            (*it)->setCheckState(TreeCol::Check, Qt::Unchecked);
        }
        currentTree->setUpdatesEnabled(true);

        QMetaObject::invokeMethod(this, [this] { findPackage(); }, Qt::QueuedConnection);
        setSearchFocus();
        ui->pushInstall->setEnabled(false);
        ui->pushUninstall->setEnabled(false);
    };

    auto uncheckAllItems = [this]() {
        // Optimization: Disable updates during bulk operations
        currentTree->setUpdatesEnabled(false);
        for (QTreeWidgetItemIterator it(currentTree); (*it) != nullptr; ++it) {
            (*it)->setCheckState(TreeCol::Check, Qt::Unchecked);
        }
        currentTree->setUpdatesEnabled(true);
    };

    auto handleFlatpakFilter = [this, uncheckAllItems](const QStringList &data, bool raw = true) {
        uncheckAllItems();
        displayFilteredFP(data, raw);
    };

    auto updateButtonStates = [this](bool installEnabled, bool uninstallEnabled) {
        ui->pushInstall->setEnabled(installEnabled);
        ui->pushUninstall->setEnabled(uninstallEnabled);
    };

    auto clearChangeListAndButtons = [this, updateButtonStates]() {
        updateButtonStates(false, false);
        changeList.clear();
    };

    auto resizeCurrentRepoTree = [this]() {
        if (currentTree == ui->treeFlatpak) {
            return;
        }
        for (int i = 0; i < currentTree->columnCount(); ++i) {
            if (!currentTree->isColumnHidden(i)) {
                currentTree->resizeColumnToContents(i);
            }
        }
    };

    auto handleAurQueryError = [this]() {
        QMessageBox::critical(this, tr("Error"),
                              tr("Could not query AUR packages. Please check that paru is installed."));
        currentTree->setUpdatesEnabled(true);
        currentTree->blockSignals(false);
    };

    auto applyAurStatusFilter = [this, &uncheckAllItems](int statusFilter) {
        QSignalBlocker blocker(currentTree);
        auto parseNameSet = [](const QStringList &lines) {
            QSet<QString> names;
            names.reserve(lines.size());
            for (const QString &line : lines) {
                const QString name = line.section(' ', 0, 0).trimmed();
                if (!name.isEmpty()) {
                    names.insert(name);
                }
            }
            return names;
        };

        const QStringList aurInstalledLines
            = cmd.getOut("pacman -Qm --color never").split('\n', Qt::SkipEmptyParts);
        const QSet<QString> aurInstalled = parseNameSet(aurInstalledLines);

        QSet<QString> aurUpgradable;
        QHash<QString, QString> aurUpgradableVersions;
        if (statusFilter == Status::Upgradable) {
            const QStringList aurUpgradableLines
                = cmd.getOut("paru -Qua --color never").split('\n', Qt::SkipEmptyParts);
            aurUpgradable.reserve(aurUpgradableLines.size());
            aurUpgradableVersions.reserve(aurUpgradableLines.size());
            for (const QString &line : aurUpgradableLines) {
                const QString name = line.section(' ', 0, 0).trimmed();
                if (name.isEmpty()) {
                    continue;
                }
                aurUpgradable.insert(name);
                const QString newVersion = line.section("->", 1).trimmed();
                if (!newVersion.isEmpty()) {
                    aurUpgradableVersions.insert(name, newVersion);
                }
            }
        }

        QSet<QString> aurAutoremovable;
        if (statusFilter == Status::Autoremovable) {
            const QStringList aurAutoremovableLines = getAutoremovablePackages();
            aurAutoremovable = QSet<QString>(aurAutoremovableLines.cbegin(), aurAutoremovableLines.cend());
        }

        bool hasVisibleMatches = false;
        for (QTreeWidgetItemIterator it(currentTree); (*it) != nullptr; ++it) {
            const QString name = (*it)->text(TreeCol::Name);
            bool shouldShow = false;
            int status = Status::NotInstalled;
            if (statusFilter == Status::Installed) {
                shouldShow = aurInstalled.contains(name);
                status = shouldShow ? Status::Installed : Status::NotInstalled;
            } else if (statusFilter == Status::Upgradable) {
                shouldShow = aurUpgradable.contains(name);
                status = shouldShow ? Status::Upgradable : Status::NotInstalled;
                if (shouldShow) {
                    const QString newVersion = aurUpgradableVersions.value(name);
                    if (!newVersion.isEmpty()) {
                        (*it)->setText(TreeCol::RepoVersion, newVersion);
                    }
                }
            } else if (statusFilter == Status::Autoremovable) {
                shouldShow = aurAutoremovable.contains(name);
                status = shouldShow ? Status::Autoremovable : Status::NotInstalled;
            } else if (statusFilter == Status::NotInstalled) {
                shouldShow = !aurInstalled.contains(name);
                status = shouldShow ? Status::NotInstalled : Status::Installed;
            }
            (*it)->setHidden(!shouldShow);
            (*it)->setData(TreeCol::Status, Qt::UserRole, status);
            (*it)->setData(0, Qt::UserRole, shouldShow);
            hasVisibleMatches = hasVisibleMatches || shouldShow;
        }

        if (statusFilter == Status::Upgradable) {
            updateTreeItems(currentTree);
        }

        if (statusFilter == Status::Upgradable || statusFilter == Status::Autoremovable) {
            if (headerAUR) {
                const QString tip = (statusFilter == Status::Upgradable) ? tr("Select/deselect all upgradable")
                                                                         : tr("Select/deselect all autoremovable");
                headerAUR->setCheckboxVisible(hasVisibleMatches);
                if (hasVisibleMatches) {
                    headerAUR->setToolTip(tip);
                    headerAUR->resizeSection(TreeCol::Check, qMax(headerAUR->sectionSize(0), 22));
                }
            }
        }

        uncheckAllItems();
    };

    // Hide and reset all header checkboxes by default
    if (headerRepo) {
        headerRepo->setCheckboxVisible(false);
        headerRepo->setChecked(false);
    }
    if (headerAUR) {
        headerAUR->setCheckboxVisible(false);
        headerAUR->setChecked(false);
    }

    // Handle Flatpak tree
    if (currentTree == ui->treeFlatpak) {
        if (arg1 == tr("Installed runtimes")) {
            handleFlatpakFilter(installedRuntimesFP, false);
            clearChangeListAndButtons();
        } else if (arg1 == tr("Installed apps")) {
            handleFlatpakFilter(installedAppsFP, false);
            clearChangeListAndButtons();
        } else if (arg1 == tr("All apps")) {
            if (flatpaksApps.isEmpty()) {
                flatpaksApps = listFlatpaks(ui->comboRemote->currentText(), "--app");
            }
            handleFlatpakFilter(flatpaksApps);
            clearChangeListAndButtons();
        } else if (arg1 == tr("All runtimes")) {
            if (flatpaksRuntimes.isEmpty()) {
                flatpaksRuntimes = listFlatpaks(ui->comboRemote->currentText(), "--runtime");
            }
            handleFlatpakFilter(flatpaksRuntimes);
            clearChangeListAndButtons();
        } else if (arg1 == tr("All available")) {
            resetTree();
            ui->labelNumAppFP->setText(QString::number(currentTree->topLevelItemCount()));
            clearChangeListAndButtons();
        } else if (arg1 == tr("All installed")) {
            displayFilteredFP(installedAppsFP + installedRuntimesFP);
        } else if (arg1 == tr("Not installed")) {
            for (QTreeWidgetItemIterator it(currentTree); (*it) != nullptr; ++it) {
                bool isNotInstalled = (*it)->data(FlatCol::Status, Qt::UserRole) == Status::NotInstalled;
                if (!isNotInstalled) {
                    (*it)->setHidden(true);
                    (*it)->setCheckState(FlatCol::Check, Qt::Unchecked);
                    changeList.removeOne((*it)->data(FlatCol::FullName, Qt::UserRole).toString());
                }
                (*it)->setData(0, Qt::UserRole, isNotInstalled);
            }
            ui->pushUninstall->setEnabled(false);
        }
        QMetaObject::invokeMethod(this, [this] { findPackage(); }, Qt::QueuedConnection);
        setSearchFocus();
    } else if (currentTree == ui->treeRepo) {
        if (!repoCacheValid && !buildRepoCache(true)) {
            currentTree->setUpdatesEnabled(true);
            currentTree->blockSignals(false);
            QMessageBox::critical(this, tr("Error"),
                                  tr("Could not query repository packages. Please check pacman output."));
            return;
        }
        startInstalledPackagesLoad();
        const int statusFilter = ui->comboFilterRepo->currentIndex();
        applyRepoFilter(statusFilter);

        if (statusFilter == Status::Upgradable || statusFilter == Status::Autoremovable) {
            bool hasVisibleMatches = false;
            for (QTreeWidgetItemIterator it(currentTree); (*it) != nullptr; ++it) {
                if (!(*it)->isHidden() && (*it)->data(TreeCol::Status, Qt::UserRole).toInt() == statusFilter) {
                    hasVisibleMatches = true;
                    break;
                }
            }
            if (headerRepo) {
                const QString tip = (statusFilter == Status::Upgradable) ? tr("Select/deselect all upgradable")
                                                                         : tr("Select/deselect all autoremovable");
                headerRepo->setCheckboxVisible(hasVisibleMatches);
                if (hasVisibleMatches) {
                    headerRepo->setToolTip(tip);
                    headerRepo->resizeSection(TreeCol::Check, qMax(headerRepo->sectionSize(0), 22));
                }
            }
        }

        QMetaObject::invokeMethod(this, [this] { findPackage(); }, Qt::QueuedConnection);
        setSearchFocus();
        clearChangeListAndButtons();
        resizeCurrentRepoTree();
        currentTree->setUpdatesEnabled(true);
        currentTree->blockSignals(false);
        return;
    } else if (currentTree == ui->treeAUR) {
        if (getParuPath().isEmpty()) {
            handleAurQueryError();
            return;
        }
        const QString searchTerm = ui->searchBoxAUR->text().trimmed();
        const bool hasSearch = searchTerm.size() >= 2;
        if (arg1 == tr("All packages")) {
            if (!buildAurList(hasSearch ? searchTerm : QString())) {
                handleAurQueryError();
                return;
            }
            dirtyAur = true;
            displayPackages();
        } else if (arg1 == tr("Not installed")) {
            if (hasSearch) {
                if (!buildAurList(searchTerm)) {
                    handleAurQueryError();
                    return;
                }
            } else {
                aurList.clear();
            }
            dirtyAur = true;
            displayPackages();
            if (hasSearch) {
                applyAurStatusFilter(Status::NotInstalled);
            }
        } else if (arg1 == tr("Installed")) {
            if (!buildAurList(QString())) {
                handleAurQueryError();
                return;
            }
            dirtyAur = true;
            displayPackages();
            applyAurStatusFilter(Status::Installed);
        } else if (arg1 == tr("Upgradable")) {
            if (!buildAurList(QString())) {
                handleAurQueryError();
                return;
            }
            dirtyAur = true;
            displayPackages();
            applyAurStatusFilter(Status::Upgradable);
        } else if (arg1 == tr("Autoremovable")) {
            if (!buildAurList(QString())) {
                handleAurQueryError();
                return;
            }
            dirtyAur = true;
            displayPackages();
            applyAurStatusFilter(Status::Autoremovable);
        }
        QMetaObject::invokeMethod(this, [this] { findPackage(); }, Qt::QueuedConnection);
        setSearchFocus();
        clearChangeListAndButtons();
        resizeCurrentRepoTree();
        currentTree->setUpdatesEnabled(true);
        currentTree->blockSignals(false);
        return;
    }
    resizeCurrentRepoTree();
    currentTree->setUpdatesEnabled(true);
    currentTree->blockSignals(false);
}

// Toggle selection of all visible upgradable items in the current tab
void MainWindow::selectAllUpgradable_toggled(bool checked)
{
    QTreeWidget *tree = nullptr;
    if (sender() == headerRepo) {
        tree = ui->treeRepo;
    } else if (sender() == headerAUR) {
        tree = ui->treeAUR;
    }
    if (!tree) {
        return;
    }

    // Batch update: avoid emitting itemChanged for every item
    tree->setUpdatesEnabled(false);
    tree->blockSignals(true);
    // Determine desired status based on current filter text
    int targetStatus = Status::Upgradable;
    QString filterText;
    if (tree == ui->treeRepo) {
        filterText = ui->comboFilterRepo->currentText();
    } else if (tree == ui->treeAUR) {
        filterText = ui->comboFilterAUR->currentText();
    }
    if (filterText == tr("Autoremovable")) {
        targetStatus = Status::Autoremovable;
    }

    for (QTreeWidgetItemIterator it(tree); *it; ++it) {
        QTreeWidgetItem *item = *it;
        const bool visible = !item->isHidden();
        const bool match = item->data(TreeCol::Status, Qt::UserRole).toInt() == targetStatus;
        if (visible && match) {
            item->setCheckState(TreeCol::Check, checked ? Qt::Checked : Qt::Unchecked);
        }
    }
    // Rebuild changeList and update buttons once, instead of per-item
    changeList.clear();
    if (checked) {
        for (QTreeWidgetItemIterator it(tree); *it; ++it) {
            QTreeWidgetItem *item = *it;
            const bool visible = !item->isHidden();
            const bool match = item->data(TreeCol::Status, Qt::UserRole).toInt() == targetStatus;
            if (visible && match && item->checkState(TreeCol::Check) == Qt::Checked) {
                changeList.append(item->text(TreeCol::Name));
            }
        }
    }

    // Update action buttons coherently after batch toggle
    ui->pushInstall->setEnabled(!changeList.isEmpty());
    if (tree != ui->treeFlatpak) {
        ui->pushUninstall->setEnabled(checkInstalled(changeList));
        if (targetStatus == Status::Autoremovable) {
            ui->pushInstall->setText(tr("Mark keep"));
        } else {
            ui->pushInstall->setText(checkUpgradable(changeList) ? tr("Upgrade") : tr("Install"));
        }
    }

    tree->blockSignals(false);
    tree->setUpdatesEnabled(true);
}

void MainWindow::treeAUR_itemChanged(QTreeWidgetItem *item)
{
    if (item->checkState(TreeCol::Check) == Qt::Checked) {
        ui->treeAUR->setCurrentItem(item);
    }
    buildChangeList(item);
}

void MainWindow::treeRepo_itemChanged(QTreeWidgetItem *item)
{
    if (item->checkState(TreeCol::Check) == Qt::Checked) {
        ui->treeRepo->setCurrentItem(item);
    }
    buildChangeList(item);
}

void MainWindow::treeFlatpak_itemChanged(QTreeWidgetItem *item)
{
    if (item->checkState(FlatCol::Check) == Qt::Checked) {
        ui->treeFlatpak->setCurrentItem(item);
    }
    buildChangeList(item);
}

// Build the changeList when selecting on item in the tree
void MainWindow::buildChangeList(QTreeWidgetItem *item)
{
    qDebug() << "+++" << __PRETTY_FUNCTION__ << "+++";
    /* if all apps are uninstalled (or some installed) -> enable Install, disable Uinstall
     * if all apps are installed or upgradable -> enable Uninstall, enable Install
     * if all apps are upgradable -> change Install label to Upgrade;
     */

    lastItemClicked = item;
    QString newapp;
    if (currentTree == ui->treeFlatpak) {
        if (changeList.isEmpty()
            && indexFilterFP.isEmpty()) { // remember the Flatpak combo location first time this is called
            indexFilterFP = ui->comboFilterFlatpak->currentText();
        }
        newapp = (item->data(FlatCol::FullName, Qt::UserRole).toString());
    } else {
        newapp = (item->text(TreeCol::Name));
    }

    if (item->checkState(0) == Qt::Checked) {
        ui->pushInstall->setEnabled(true);
        changeList.append(newapp);
    } else {
        changeList.removeOne(newapp);
    }

    if (currentTree != ui->treeFlatpak) {
        ui->pushUninstall->setEnabled(checkInstalled(changeList));
        ui->pushInstall->setText(checkUpgradable(changeList) ? tr("Upgrade") : tr("Install"));
        if ((currentTree == ui->treeRepo && ui->comboFilterRepo->currentText() == tr("Autoremovable"))
            || (currentTree == ui->treeAUR && ui->comboFilterAUR->currentText() == tr("Autoremovable"))) {
            ui->pushInstall->setText(tr("Mark keep"));
        }
    } else { // For Flatpaks allow selection only of installed or not installed items so one clicks
             // on an installed item only installed items should be displayed and the other way round
        ui->pushInstall->setText(tr("Install"));
        if (item->data(FlatCol::Status, Qt::UserRole) == Status::Installed) {
            if (item->checkState(FlatCol::Check) == Qt::Checked
                && ui->comboFilterFlatpak->currentText() != tr("All installed")) {
                ui->comboFilterFlatpak->setCurrentText(tr("All installed"));
            }
            ui->pushUninstall->setEnabled(true);
            ui->pushInstall->setEnabled(false);
        } else {
            if (item->checkState(FlatCol::Check) == Qt::Checked
                && ui->comboFilterFlatpak->currentText() != tr("Not installed")) {
                ui->comboFilterFlatpak->setCurrentText(tr("Not installed"));
            }
            ui->pushUninstall->setEnabled(false);
            ui->pushInstall->setEnabled(true);
        }
        if (changeList.isEmpty()) { // Reset comboFilterFlatpak if nothing is selected
            ui->comboFilterFlatpak->setCurrentText(indexFilterFP);
            indexFilterFP.clear();
        }
        ui->treeFlatpak->setFocus();
    }

    if (changeList.isEmpty()) {
        ui->pushInstall->setEnabled(false);
        ui->pushUninstall->setEnabled(false);
    }
}

void MainWindow::pushForceUpdateMX_clicked()
{
    ui->searchBoxAUR->clear();
    ui->comboFilterAUR->setCurrentIndex(0);
    buildPackageLists(true);
    updateInterface();
}

void MainWindow::pushForceUpdateRepo_clicked()
{
    ui->searchBoxRepo->clear();
    ui->comboFilterRepo->setCurrentIndex(0);
    updateRepos();
    repoCacheValid = false;
    buildRepoCache(true);
    applyRepoFilter(ui->comboFilterRepo->currentIndex());
    updateInterface();
}

void MainWindow::pushForceUpdateFP_clicked()
{
    ui->searchBoxFlatpak->clear();
    if (!flatpakCancelHidden && pushCancel) {
        pushCancel->setEnabled(false);
        pushCancel->hide();
        flatpakCancelHidden = true;
    }
    holdProgressForFlatpakRefresh = true;
    progress->show();
    cmd.run("flatpak update --appstream");
    displayFlatpaks(true);
    updateInterface();
}


// Pressing Enter or buttonEnter will do the same thing
void MainWindow::pushEnter_clicked()
{
    if (currentTree == ui->treeFlatpak
        && ui->lineEdit->text().isEmpty()) { // Add "Y" as default response for flatpaks to work like pacman
        cmd.write("y");
    }
    lineEdit_returnPressed();
}

void MainWindow::lineEdit_returnPressed()
{
    qDebug() << "+++" << __PRETTY_FUNCTION__ << "+++";
    cmd.write(ui->lineEdit->text().toUtf8() + '\n');
    ui->outputBox->appendPlainText(ui->lineEdit->text() + '\n');
    ui->lineEdit->clear();
    ui->lineEdit->setFocus();
}

void MainWindow::pushCancel_clicked()
{
    qDebug() << "+++" << __PRETTY_FUNCTION__ << "+++";
    if (cmd.state() != QProcess::NotRunning) {
        if (QMessageBox::warning(this, tr("Quit?"),
                                 tr("Process still running, quitting might leave the system in an unstable "
                                    "state.<p><b>Are you sure you want to exit MX Package Installer?</b>"),
                                 QMessageBox::Yes, QMessageBox::No)
            == QMessageBox::No) {
            return;
        }
    }
    cleanup();
    QApplication::quit();
}

// On change flatpak remote
void MainWindow::comboRemote_activated(int /*index*/)
{
    qDebug() << "+++" << __PRETTY_FUNCTION__ << "+++";
    lastItemClicked = nullptr;
    displayFlatpaks(true);
}

void MainWindow::pushUpgradeFP_clicked()
{
    qDebug() << "+++" << __PRETTY_FUNCTION__ << "+++";
    showOutput();
    setCursor(QCursor(Qt::BusyCursor));
    enableOutput();
    if (cmd.run("socat SYSTEM:'flatpak update " + fpUser + "',pty STDIO")) {
        displayFlatpaks(true);
        setCursor(QCursor(Qt::ArrowCursor));
        QMessageBox::information(this, tr("Done"), tr("Processing finished successfully."));
        ui->tabWidget->setCurrentWidget(ui->tabFlatpak);
    } else {
        setCursor(QCursor(Qt::ArrowCursor));
        QMessageBox::critical(this, tr("Error"),
                              tr("Problem detected while installing, please inspect the console output."));
    }
    enableTabs(true);
}

void MainWindow::pushRemotes_clicked()
{
    qDebug() << "+++" << __PRETTY_FUNCTION__ << "+++";
    auto *dialog = new ManageRemotes(this, fpUser);
    dialog->exec();
    if (dialog->isChanged()) {
        invalidateFlatpakRemoteCache();
        listFlatpakRemotes();
        displayFlatpaks(true);
    }
    if (!dialog->getInstallRef().isEmpty()) {
        showOutput();
        setCursor(QCursor(Qt::BusyCursor));
        enableOutput();
        if (cmd.run("socat SYSTEM:'flatpak install -y " + dialog->getUser() + "--from "
                    + shellQuote(dialog->getInstallRef()) + "',stderr STDIO\"")) {
            invalidateFlatpakRemoteCache();
            listFlatpakRemotes();
            displayFlatpaks(true);
            setCursor(QCursor(Qt::ArrowCursor));
            QMessageBox::information(this, tr("Done"), tr("Processing finished successfully."));
            ui->tabWidget->blockSignals(true);
            ui->tabWidget->setCurrentWidget(ui->tabFlatpak);
            ui->tabWidget->blockSignals(false);
        } else {
            setCursor(QCursor(Qt::ArrowCursor));
            QMessageBox::critical(this, tr("Error"),
                                  tr("Problem detected while installing, please inspect the console output."));
        }
        enableTabs(true);
    }
}

void MainWindow::comboUser_currentIndexChanged(int index)
{
    qDebug() << "+++" << __PRETTY_FUNCTION__ << "+++";
    if (index == 0) {
        fpUser = "--system ";
    } else {
        fpUser = "--user ";
        static bool updated = false;
        if (!updated) {
            setCursor(QCursor(Qt::BusyCursor));
            enableOutput();
            Cmd().run("/usr/lib/mx-packageinstaller/mxpi-lib flatpak_add_repos_user", Cmd::QuietMode::Yes);
            setCursor(QCursor(Qt::ArrowCursor));
            updated = true;
        }
    }
    lastItemClicked = nullptr;
    invalidateFlatpakRemoteCache();
    listFlatpakRemotes();
    displayFlatpaks(true);
}

// Process keystrokes
void MainWindow::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape) {
        pushCancel_clicked();
    } else if (event->matches(QKeySequence::Find)
               || (event->modifiers() == Qt::ControlModifier && event->key() == Qt::Key_F)) {
        if (ui->tabWidget->currentWidget() == ui->tabRepos) {
            ui->searchBoxRepo->setFocus();
        } else if (ui->tabWidget->currentWidget() == ui->tabMXtest) {
            ui->searchBoxAUR->setFocus();
        } else if (ui->tabWidget->currentWidget() == ui->tabFlatpak) {
            ui->searchBoxFlatpak->setFocus();
        }
    }
}

void MainWindow::pushRemoveUnused_clicked()
{
    qDebug() << "+++" << __PRETTY_FUNCTION__ << "+++";
    showOutput();
    setCursor(QCursor(Qt::BusyCursor));
    enableOutput();
    if (cmd.run("socat SYSTEM:'flatpak uninstall --unused -y',pty STDIO")) {
        displayFlatpaks(true);
        setCursor(QCursor(Qt::ArrowCursor));
        QMessageBox::information(this, tr("Done"), tr("Processing finished successfully."));
        ui->tabWidget->setCurrentWidget(ui->tabFlatpak);
    } else {
        setCursor(QCursor(Qt::ArrowCursor));
        QMessageBox::critical(this, tr("Error"),
                              tr("Problem detected during last operation, please inspect the console output."));
    }
    enableTabs(true);
}
