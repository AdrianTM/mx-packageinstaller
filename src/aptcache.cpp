#include "aptcache.h"

#include <QDebug>
#include <QDirIterator>
#include <QProcess>
#include <QStandardPaths>
#include <QRegularExpression>
#include <QStringView>

#include "versionnumber.h"

AptCache::AptCache()
    : arch(getArch())
{
    if (!isDirValid()) {
        qWarning() << "Pacman sync directory is not valid:" << dir.path();
        return;
    }

    // Pre-allocate map capacity for expected package count to reduce rehashing
    candidates.reserve(100000);

    loadCacheFiles();
}

void AptCache::loadCacheFiles()
{
    QDirIterator it(dir.path(), {"*.db"}, QDir::Files);
    while (it.hasNext()) {
        const QString fileName = it.next();
        if (!readFile(fileName)) {
            qWarning() << "Error reading sync db file:" << fileName << "-"
                       << QFile(dir.absoluteFilePath(fileName)).errorString();
        }
    }
}

const QHash<QString, PackageInfo>& AptCache::getCandidates() const
{
    return candidates;
}

// Return pacman arch format
QString AptCache::getArch()
{
    return arch_names.value(QSysInfo::currentCpuArchitecture(), QStringLiteral("unknown"));
}

bool AptCache::isDirValid() const
{
    return dir.exists() && dir.isReadable();
}

void AptCache::updateCandidate(const QString &package, const QString &version, const QString &description)
{
    auto it = candidates.find(package);
    if (it == candidates.end()) {
        candidates.insert(package, {version, description});
    } else {
        const VersionNumber currentVersion(it->version);
        const VersionNumber newVersion(version);
        if (currentVersion < newVersion) {
            it->version = version;
            it->description = description;
        }
    }
}

bool AptCache::readFile(const QString &fileName)
{
    const QString filePath = dir.absoluteFilePath(fileName);
    const QString bsdtar = QStandardPaths::findExecutable("bsdtar");
    const QString program = bsdtar.isEmpty() ? QStringLiteral("tar") : bsdtar;

    auto runExtract = [&](const QStringList &args, QByteArray &output) {
        QProcess proc;
        proc.setProgram(program);
        proc.setArguments(args);
        proc.start();
        if (!proc.waitForFinished()) {
            return false;
        }
        output = proc.readAllStandardOutput();
        return proc.exitStatus() == QProcess::NormalExit && proc.exitCode() == 0;
    };

    QByteArray output;
    bool ok = runExtract({"-xOf", filePath, "*/desc"}, output);
    if ((!ok || output.isEmpty()) && bsdtar.isEmpty()) {
        output.clear();
        ok = runExtract({"--zstd", "-xOf", filePath, "*/desc"}, output);
    }
    if (!ok || output.isEmpty()) {
        qWarning() << "Could not read pacman db:" << fileName;
        return false;
    }
    parseFileContent(QString::fromUtf8(output));
    return true;
}

void AptCache::parseFileContent(const QString &content)
{
    QTextStream stream(const_cast<QString*>(&content));
    QString line;
    QString package;
    QString version;
    QString description;
    QString pkgArch;

    auto flushPackage = [&]() {
        if (!package.isEmpty() && !version.isEmpty()
            && (pkgArch == arch || pkgArch == QLatin1String("any"))) {
            updateCandidate(package, version, description);
        }
        package.clear();
        version.clear();
        description.clear();
        pkgArch.clear();
    };

    enum class Section { None, Name, Version, Desc, Arch };
    Section current = Section::None;

    while (stream.readLineInto(&line)) {
        if (line == QLatin1String("%NAME%")) {
            flushPackage();
            current = Section::Name;
            continue;
        }
        if (line == QLatin1String("%VERSION%")) {
            current = Section::Version;
            continue;
        }
        if (line == QLatin1String("%DESC%")) {
            current = Section::Desc;
            continue;
        }
        if (line == QLatin1String("%ARCH%")) {
            current = Section::Arch;
            continue;
        }
        if (line.startsWith('%')) {
            current = Section::None;
            continue;
        }

        const QString value = line.trimmed();
        if (value.isEmpty()) {
            continue;
        }

        switch (current) {
        case Section::Name:
            package = value;
            break;
        case Section::Version:
            version = value;
            break;
        case Section::Desc:
            description = value;
            break;
        case Section::Arch:
            pkgArch = value;
            break;
        case Section::None:
            break;
        }
    }

    flushPackage();
}
