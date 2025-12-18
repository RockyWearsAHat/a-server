#include "common/AssetPaths.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>

namespace AIO::Common {

static QString normalizeRel(const QString& rel)
{
    QString r = rel;
    while (r.startsWith('/')) r.remove(0, 1);
    return r;
}

QString AssetsRoot()
{
    if (qEnvironmentVariableIsSet("AIO_ASSETS_DIR")) {
        const QString env = qEnvironmentVariable("AIO_ASSETS_DIR");
        if (!env.isEmpty() && QFileInfo::exists(env)) {
            return QDir(env).absolutePath();
        }
    }

    const QString appDir = QCoreApplication::applicationDirPath();

    const QString cand1 = QDir(appDir + "/../assets").absolutePath();
    if (QFileInfo::exists(cand1)) {
        return cand1;
    }

    const QString cand2 = QDir(appDir + "/../../assets").absolutePath();
    if (QFileInfo::exists(cand2)) {
        return cand2;
    }

    return cand1;
}

QString AssetPath(const QString& relative)
{
    return QDir(AssetsRoot()).absoluteFilePath(normalizeRel(relative));
}

} // namespace AIO::Common
