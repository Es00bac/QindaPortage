// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QJsonObject>
#include <QString>
#include <QStringList>

namespace QindaPortage::Core {

// A failed build log reduced to what a small model can act on: the failed
// phase, a classification, the first real errors with a little context,
// and the tail (docs/design.md "Keeping the model small in context").
struct BuildFailure
{
    QString cpv;
    QString phase;          // "compile", "configure", "install", "prepare", ...
    QString kind;           // missing-header, missing-library, undefined-reference,
                            // compiler-error, cmake-config, meson-config,
                            // configure-check, patch-failed, fetch-failed,
                            // test-failure, sandbox, out-of-memory, unknown
    QStringList missing;    // headers, libraries or modules named in the errors
    QStringList errors;     // first error lines with context (bounded)
    QStringList tail;       // last lines of the log
    QString logPath;

    QJsonObject toJson() const;
};

class BuildLog
{
public:
    // Locates the newest build.log for "cat/pn" (or cat/pn-ver) under
    // PORTAGE_TMPDIR (/var/tmp/portage by default).
    static QString find(const QString &atomOrKey, const QString &tmpdir = QStringLiteral("/var/tmp/portage"));
    static BuildFailure analyse(const QString &text, int maxErrorLines = 40, int tailLines = 25);
    static BuildFailure analyseFile(const QString &path);
};

} // namespace QindaPortage::Core
