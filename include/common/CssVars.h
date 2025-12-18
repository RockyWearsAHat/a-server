#pragma once

#include <QMap>
#include <QString>

namespace AIO::Common {

struct CssVarSets {
    QMap<QString, QString> light;
    QMap<QString, QString> dark;
};

// Extracts CSS custom properties from:
//   :root { --name: value; }
// and
//   @media (prefers-color-scheme: dark) { :root { --name: value; } }
//
// This is intentionally minimal (not a general CSS parser). It exists to keep
// a single source of truth for theme tokens between the NAS web UI (CSS) and
// the native Qt NAS page (QSS).
CssVarSets ExtractCssVars(const QString& cssText);

CssVarSets LoadCssVarsFromFile(const QString& filePath);

// Returns the best-effort value for `--key` in the chosen palette.
QString GetCssVarOr(const QMap<QString, QString>& vars, const QString& key, const QString& fallback);

} // namespace AIO::Common
