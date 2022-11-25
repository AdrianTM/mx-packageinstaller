
#ifndef APTCACHE_H
#define APTCACHE_H

#include <QFile>
#include <QHash>
#include <QMap>

#include "versionnumber.h"

// Pair of arch names returned by "uname" and corresponding DEB_BUILD_ARCH formats
static const QHash<QString, QString> arch_names {{"x86_64", "amd64"}, {"i686", "i386"}, {"armv7l", "armhf"}};

class AptCache
{
public:
    AptCache();

    void loadCacheFiles();
    QMap<QString, QStringList> getCandidates();
    static QString getArch();

private:
    QMap<QString, QStringList> candidates;
    QString files_content;
    const QString dir_name = QStringLiteral("/var/lib/apt/lists/");

    void parseContent();
    bool readFile(const QString &file_name);
};

#endif // APTCACHE_H
