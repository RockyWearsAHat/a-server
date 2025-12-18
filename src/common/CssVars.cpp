#include "common/CssVars.h"

#include <QFile>
#include <QRegularExpression>

namespace AIO::Common {

static QMap<QString, QString> parseRootVars(const QString& cssBlock)
{
    QMap<QString, QString> vars;

    // Capture inside ":root { ... }" within the provided block.
    QRegularExpression rootRe(R"(:root\s*\{([^}]*)\})", QRegularExpression::DotMatchesEverythingOption);
    auto rootMatch = rootRe.match(cssBlock);
    if (!rootMatch.hasMatch()) {
        return vars;
    }

    const QString rootBody = rootMatch.captured(1);

    // Capture "--key: value;" pairs.
    QRegularExpression varRe(R"(--([A-Za-z0-9_-]+)\s*:\s*([^;\n\r]+)\s*;)" );
    auto it = varRe.globalMatch(rootBody);
    while (it.hasNext()) {
        const auto m = it.next();
        const QString key = m.captured(1).trimmed();
        const QString value = m.captured(2).trimmed();
        if (!key.isEmpty() && !value.isEmpty()) {
            vars.insert(key, value);
        }
    }

    return vars;
}

CssVarSets ExtractCssVars(const QString& cssText)
{
    CssVarSets sets;

    // Light/default: use the first top-level :root block if present.
    sets.light = parseRootVars(cssText);

    // Dark: find a prefers-color-scheme: dark media block, then parse its :root.
    QRegularExpression darkMediaRe(
        R"(@media\s*\(\s*prefers-color-scheme\s*:\s*dark\s*\)\s*\{([^}]*(\}[^}]*)*)\})",
        QRegularExpression::DotMatchesEverythingOption);
    auto darkMatch = darkMediaRe.match(cssText);
    if (darkMatch.hasMatch()) {
        const QString darkBlock = darkMatch.captured(1);
        sets.dark = parseRootVars(darkBlock);
    }

    return sets;
}

CssVarSets LoadCssVarsFromFile(const QString& filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return {};
    }

    const QString cssText = QString::fromUtf8(file.readAll());
    return ExtractCssVars(cssText);
}

QString GetCssVarOr(const QMap<QString, QString>& vars, const QString& key, const QString& fallback)
{
    const auto it = vars.find(key);
    if (it == vars.end()) {
        return fallback;
    }
    return it.value();
}

} // namespace AIO::Common
