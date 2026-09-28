// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/buildlog.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QRegularExpression>

namespace QindaPortage::Core {

QJsonObject BuildFailure::toJson() const
{
    return {{QStringLiteral("cpv"), cpv}, {QStringLiteral("phase"), phase}, {QStringLiteral("kind"), kind},
            {QStringLiteral("missing"), QJsonArray::fromStringList(missing)},
            {QStringLiteral("errors"), errors.join(QLatin1Char('\n'))},
            {QStringLiteral("tail"), tail.join(QLatin1Char('\n'))}, {QStringLiteral("log"), logPath}};
}

QString BuildLog::find(const QString &atomOrKey, const QString &tmpdir)
{
    QString key = atomOrKey.trimmed();
    while (!key.isEmpty() && QStringLiteral("=<>~").contains(key.front())) key.remove(0, 1);
    const QString category = key.section(QLatin1Char('/'), 0, 0);
    const QString rest = key.section(QLatin1Char('/'), 1);
    QString newest;
    QDateTime newestTime;
    QDir catDir(tmpdir + QLatin1Char('/') + category);
    for (const QString &pf : catDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        if (pf != rest && !pf.startsWith(rest + QLatin1Char('-'))) continue;
        const QFileInfo log(catDir.filePath(pf + QStringLiteral("/temp/build.log")));
        if (log.exists() && (newest.isEmpty() || log.lastModified() > newestTime)) {
            newest = log.absoluteFilePath();
            newestTime = log.lastModified();
        }
    }
    return newest;
}

BuildFailure BuildLog::analyse(const QString &text, int maxErrorLines, int tailLines)
{
    BuildFailure f;
    const QStringList lines = text.split(QLatin1Char('\n'));
    static const QRegularExpression failedRe(QStringLiteral("\\* ERROR: (\\S+) failed \\((\\w+) phase\\)"));
    static const QRegularExpression headerRe(QStringLiteral("fatal error: ([^:]+): No such file or directory"));
    static const QRegularExpression libRe(QStringLiteral("cannot find -l(\\S+)"));
    static const QRegularExpression undefRe(QStringLiteral("undefined reference to [`']([^'`]+)'"));
    static const QRegularExpression cmakePkgRe(QStringLiteral("Could not find a package configuration file provided by \"([^\"]+)\"|Could NOT find (\\S+)"));
    static const QRegularExpression mesonDepRe(QStringLiteral("Dependency \"([^\"]+)\" not found|Run-time dependency (\\S+) found: NO"));
    static const QRegularExpression pkgConfigRe(QStringLiteral("Package '([^']+)', required by .*, not found|No package '([^']+)' found"));
    static const QRegularExpression pyModRe(QStringLiteral("ModuleNotFoundError: No module named '([^']+)'"));
    static const QRegularExpression errorRe(QStringLiteral("(\\berror:|\\bError \\d|CMake Error|ERROR:|undefined reference|ninja: build stopped|make(\\[\\d+\\])?: \\*\\*\\*|Traceback \\(most recent call last\\)|FAILED:)"));

    auto addMissing = [&f](const QString &name) {
        if (!name.isEmpty() && !f.missing.contains(name)) f.missing.append(name);
    };
    int firstError = -1;
    for (int i = 0; i < lines.size(); ++i) {
        const QString &l = lines.at(i);
        if (const auto m = failedRe.match(l); m.hasMatch()) {
            f.cpv = m.captured(1);
            f.phase = m.captured(2);
        }
        if (const auto m = headerRe.match(l); m.hasMatch()) { addMissing(m.captured(1)); if (f.kind.isEmpty()) f.kind = QStringLiteral("missing-header"); }
        if (const auto m = libRe.match(l); m.hasMatch()) { addMissing(QStringLiteral("lib") + m.captured(1)); if (f.kind.isEmpty()) f.kind = QStringLiteral("missing-library"); }
        if (const auto m = cmakePkgRe.match(l); m.hasMatch()) { addMissing(m.captured(1) + m.captured(2)); if (f.kind.isEmpty()) f.kind = QStringLiteral("cmake-config"); }
        if (const auto m = mesonDepRe.match(l); m.hasMatch()) { addMissing(m.captured(1) + m.captured(2)); if (f.kind.isEmpty()) f.kind = QStringLiteral("meson-config"); }
        if (const auto m = pkgConfigRe.match(l); m.hasMatch()) { addMissing(m.captured(1) + m.captured(2)); if (f.kind.isEmpty()) f.kind = QStringLiteral("configure-check"); }
        if (const auto m = pyModRe.match(l); m.hasMatch()) { addMissing(m.captured(1)); if (f.kind.isEmpty()) f.kind = QStringLiteral("missing-python-module"); }
        if (const auto m = undefRe.match(l); m.hasMatch() && f.kind.isEmpty()) f.kind = QStringLiteral("undefined-reference");
        if (l.contains(QLatin1String("Hunk #")) && l.contains(QLatin1String("FAILED"))) f.kind = QStringLiteral("patch-failed");
        if (l.contains(QLatin1String("ACCESS DENIED")) || l.contains(QLatin1String("sandbox violation"))) f.kind = QStringLiteral("sandbox");
        if (l.contains(QLatin1String("Killed signal terminated program")) || l.contains(QLatin1String("virtual memory exhausted"))
            || l.contains(QLatin1String("out of memory"))) {
            f.kind = QStringLiteral("out-of-memory");
        }
        if (firstError < 0 && errorRe.match(l).hasMatch() && !l.contains(QLatin1String("-Werror"))) firstError = i;
    }
    if (f.kind.isEmpty()) {
        if (f.phase == QLatin1String("test")) f.kind = QStringLiteral("test-failure");
        else if (f.phase == QLatin1String("unpack") || f.phase == QLatin1String("fetch")) f.kind = QStringLiteral("fetch-failed");
        else if (firstError >= 0) f.kind = QStringLiteral("compiler-error");
        else f.kind = QStringLiteral("unknown");
    }
    // First errors with two lines of context before each, bounded.
    if (firstError >= 0) {
        int last = -10;
        for (int i = firstError; i < lines.size() && f.errors.size() < maxErrorLines; ++i) {
            if (!errorRe.match(lines.at(i)).hasMatch()) continue;
            for (int c = std::max({0, last + 1, i - 2}); c <= i && f.errors.size() < maxErrorLines; ++c) {
                f.errors.append(lines.at(c).left(300));
            }
            last = i;
        }
    }
    int end = lines.size();
    while (end > 0 && lines.at(end - 1).trimmed().isEmpty()) --end;
    for (int i = std::max(0, end - tailLines); i < end; ++i) f.tail.append(lines.at(i).left(300));
    return f;
}

BuildFailure BuildLog::analyseFile(const QString &path)
{
    QFile file(path);
    BuildFailure f;
    if (!file.open(QIODevice::ReadOnly)) {
        f.kind = QStringLiteral("unreadable");
        f.logPath = path;
        return f;
    }
    // Logs can be hundreds of MB; errors are near the end.
    const qint64 size = file.size();
    if (size > 4 * 1024 * 1024) file.seek(size - 4 * 1024 * 1024);
    f = analyse(QString::fromUtf8(file.readAll()));
    f.logPath = path;
    return f;
}

} // namespace QindaPortage::Core
