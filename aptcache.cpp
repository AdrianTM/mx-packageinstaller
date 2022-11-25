#include "aptcache.h"

#include <QDebug>
#include <QDir>
#include <QRegularExpression>

#include "cmd.h"

AptCache::AptCache() { loadCacheFiles(); }

void AptCache::loadCacheFiles()
{
    QDir dir(dir_name);
    // include all _Packages list files
    const QString packages_filter = QStringLiteral("*_Packages");

    // some regexp's
    // to include those which match architecure in filename
    const QRegularExpression re_binary_arch(".*binary-" + getArch() + "_Packages");
    // to include those flat-repos's which do not have 'binary' within the name
    const QRegularExpression re_binary_other(QStringLiteral(".*binary-.*_Packages"));
    // to exclude debian backports
    const QRegularExpression re_backports(QStringLiteral(".*debian_.*-backports_.*_Packages"));
    // to exclude mx testrepo
    const QRegularExpression re_testrepo(QStringLiteral(".*mx_testrepo.*_test_.*_Packages"));
    // to exclude devoloper's mx temp repo
    const QRegularExpression re_temprepo(QStringLiteral(".*mx_repo.*_temp_.*_Packages"));

    const QStringList packages_files = dir.entryList(QStringList() << packages_filter, QDir::Files, QDir::Unsorted);
    QStringList files;
    for (const QString &file_name : packages_files) {
        if (re_backports.match(file_name).hasMatch() || re_testrepo.match(file_name).hasMatch()
            || re_temprepo.match(file_name).hasMatch()) {
            continue;
        }
        if (re_binary_arch.match(file_name).hasMatch()) {
            files << file_name;
            continue;
        }
        if (!re_binary_other.match(file_name).hasMatch()) {
            files << file_name;
            continue;
        }
    }

    for (const QString &file_name : qAsConst(files))
        if (!readFile(file_name))
            qDebug() << "error reading a cache file";
    parseContent();
}

QMap<QString, QStringList> AptCache::getCandidates() { return candidates; }

// return DEB_BUILD_ARCH format which differs from what 'arch' returns
QString AptCache::getArch()
{
    Cmd cmd;
    return arch_names.value(cmd.getCmdOut(QStringLiteral("arch"), true));
}

void AptCache::parseContent()
{
    const QStringList list = files_content.split(QStringLiteral("\n"));
    QStringList package_list;
    QStringList version_list;
    QStringList description_list;
    package_list.reserve(list.size());
    version_list.reserve(list.size());
    description_list.reserve(list.size());

    QString package;
    QString version;
    QString description;
    QString architecture;

    const QRegularExpression re_arch(".*(" + getArch() + "|all).*");
    bool match_arch = false;
    bool add_package = false;

    // FIXME: add deb822-format handling
    // assumption for now is made "Description:" line is always the last
    for (QString line : list) {
        if (line.startsWith(QLatin1String("Package: "))) {
            package = line.remove(QLatin1String("Package: "));
        } else if (line.startsWith(QLatin1String("Architecture:"))) {
            architecture = line.remove(QLatin1String("Architecture:")).trimmed();
            match_arch = re_arch.match(architecture).hasMatch();
        } else if (line.startsWith(QLatin1String("Version: "))) {
            version = line.remove(QLatin1String("Version: "));
        } else if (line.startsWith(QLatin1String("Description:"))) { // not "Description: " because some people don't
                                                                     // add description to their packages
            description = line.remove(QLatin1String("Description:")).trimmed();
            if (match_arch)
                add_package = true;
        }
        // add only packages with correct architecure
        if (add_package && match_arch) {
            package_list << package;
            version_list << version;
            description_list << description;
            package = QLatin1String("");
            version = QLatin1String("");
            description = QLatin1String("");
            architecture = QLatin1String("");
            add_package = false;
            match_arch = false;
        }
    }
    for (int i = 0; i < package_list.size(); ++i) {
        if (candidates.contains(package_list.at(i))
            && (VersionNumber(version_list.at(i)) <= VersionNumber(candidates.value(package_list.at(i)).at(0))))
            continue;
        candidates.insert(package_list.at(i), QStringList() << version_list.at(i) << description_list.at(i));
    }
}

bool AptCache::readFile(const QString &file_name)
{
    QFile file(dir_name + file_name);
    if (!file.open(QFile::ReadOnly)) {
        qDebug() << "Could not open file: " << file.fileName();
        return false;
    }
    files_content += file.readAll();
    file.close();
    return true;
}
