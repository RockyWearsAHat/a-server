#pragma once

#include <QString>

namespace AIO::Common {

// Returns the absolute path to the project's `assets/` directory.
//
// Resolution order:
//  1) `AIO_ASSETS_DIR` env var (if set)
//  2) `<appDir>/../assets`
//  3) `<appDir>/../../assets`
//
// If nothing exists, returns candidate (2) anyway (so callers can still use it
// as a default path for error messages).
QString AssetsRoot();

// Returns an absolute path for a file/subdir under `assets/`.
// Example: AssetPath("qss/tv.qss") -> /.../assets/qss/tv.qss
QString AssetPath(const QString& relative);

} // namespace AIO::Common
