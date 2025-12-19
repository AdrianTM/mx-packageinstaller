#pragma once

#include <QDir>
#include <QHash>
#include <QMap>
#include <QString>

// Pair of arch names returned by QSysInfo::currentCpuArchitecture() and corresponding pacman formats
inline static const QMap<QString, QString> arch_names {
    {"x86_64", "x86_64"},
    {"i686", "i686"},
    {"i386", "i686"},
    {"arm", "arm"},
    {"arm64", "aarch64"},
    {"aarch64", "aarch64"}};

struct PackageInfo {
    QString version;
    QString description;
};

class AptCache
{
public:
    AptCache();

    [[nodiscard]] const QHash<QString, PackageInfo>& getCandidates() const;
    [[nodiscard]] static QString getArch();

private:
    QHash<QString, PackageInfo> candidates;
    QString arch;
    const QDir dir {"/var/lib/pacman/sync/"};

    [[nodiscard]] bool isDirValid() const;
    [[nodiscard]] bool readFile(const QString &fileName);
    void loadCacheFiles();
    void parseFileContent(const QString &content);
    void updateCandidate(const QString &package, const QString &version, const QString &description);
};
