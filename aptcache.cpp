#include "aptcache.h"

#include <QDebug>
#include <QRegularExpression>

AptCache::AptCache() { loadCacheFiles(); }

void AptCache::loadCacheFiles()
{
    // include all _Packages list files
    const QString packages_filter = QStringLiteral("*_Packages");

    // some regexps
    // to include those which match architecture in filename
    const QRegularExpression re_binary_arch(".*binary-" + getArch() + "_Packages");
    // to include those flat-repos which do not have 'binary' within the name
    const QRegularExpression re_binary_other(QStringLiteral(".*binary-.*_Packages"));
    // to exclude Debian backports
    const QRegularExpression re_backports(QStringLiteral(".*debian_.*-backports_.*_Packages"));
    // to exclude MX testrepo
    const QRegularExpression re_testrepo(QStringLiteral(".*mx_testrepo.*_test_.*_Packages"));
    // to exclude developer's MX temp repo
    const QRegularExpression re_temprepo(QStringLiteral(".*mx_repo.*_temp_.*_Packages"));

    const QStringList packages_files = dir.entryList({packages_filter}, QDir::Files, QDir::Unsorted);
    QStringList files;
    for (const QString &file_name : packages_files) {
        if (!re_backports.match(file_name).hasMatch() && !re_testrepo.match(file_name).hasMatch()
            && !re_temprepo.match(file_name).hasMatch()) {
            if (re_binary_arch.match(file_name).hasMatch() || re_binary_other.match(file_name).hasMatch())
                files << file_name;
        }
    }
    for (const QString &file_name : qAsConst(files)) {
        if (!readFile(file_name))
            qDebug() << "error reading a cache file";
    }
    parseContent();
}

QMap<QString, QStringList> AptCache::getCandidates() { return candidates; }

// return DEB_BUILD_ARCH format which differs from what 'arch' or currentCpuArchitecture return
QString AptCache::getArch() { return arch_names.value(QSysInfo::currentCpuArchitecture()); }

void AptCache::parseContent()
{
    const QStringList list = files_content.split(QStringLiteral("\n"));

    QStringRef package;
    QStringRef version;
    QStringRef description;
    QStringRef architecture;

    const QRegularExpression re_arch(".*(" + getArch() + "|all).*");
    bool match_arch = false;

    // Code assumes Description: is the last matched line
    for (const QString &line : list) {
        if (line.startsWith(QLatin1String("Package:"))) {
            package = line.midRef(9);
        } else if (line.startsWith(QLatin1String("Architecture:"))) {
            architecture = line.midRef(14).trimmed();
            match_arch = re_arch.match(architecture).hasMatch();
        } else if (line.startsWith(QLatin1String("Version:"))) {
            version = line.midRef(9);
        } else if (line.startsWith(QLatin1String("Description:"))) {
            description = line.midRef(13).trimmed();
            if (match_arch) {
                candidates.insert(package.toString(), {version.toString(), description.toString()});
                // clear the variables for the next package
                package = QStringRef();
                version = QStringRef();
                description = QStringRef();
                architecture = QStringRef();
                match_arch = false;
            }
        }
    }
}

bool AptCache::readFile(const QString &file_name)
{
    QFile file(dir.absoluteFilePath(file_name));
    if (!file.open(QFile::ReadOnly)) {
        qWarning() << "Could not open file: " << file.fileName();
        return false;
    }
    files_content += file.readAll();
    file.close();
    return true;
}
