// SPDX-License-Identifier: GPL-3.0-or-later
#include "ops/sourceinfo.h"

#include <QJsonArray>

namespace QindaPortage::Ops {

QJsonObject SourceInfo::toJson(bool includeFiles, int fileChars) const
{
    QJsonObject o{
        {QStringLiteral("url"), url}, {QStringLiteral("host"), host}, {QStringLiteral("name"), name},
        {QStringLiteral("description"), description}, {QStringLiteral("homepage"), homepage},
        {QStringLiteral("license"), license}, {QStringLiteral("spdxLicense"), spdxLicense},
        {QStringLiteral("version"), version}, {QStringLiteral("snapshot"), snapshot},
        {QStringLiteral("buildSystem"), buildSystem}, {QStringLiteral("files"), QJsonArray::fromStringList(files)}};
    auto put = [&o](const char *key, const QString &value) {
        if (!value.isEmpty()) o[QString::fromLatin1(key)] = value;
    };
    put("owner", owner);
    put("repo", repo);
    put("language", language);
    put("tag", tag);
    put("commit", commit);
    put("commitDate", commitDate);
    put("tarballUrl", tarballUrl);
    put("archiveDir", archiveDir);
    put("gitUrl", gitUrl);
    put("defaultBranch", defaultBranch);
    put("pep517Backend", pep517Backend);
    put("error", error);
    if (!topics.isEmpty()) o[QStringLiteral("topics")] = QJsonArray::fromStringList(topics);
    if (libraryOnly) o[QStringLiteral("libraryOnly")] = true;
    if (pythonScripts) o[QStringLiteral("pythonScripts")] = true;
    QJsonArray deps;
    for (const DeclaredDependency &d : dependencies) deps.append(d.toJson());
    o[QStringLiteral("dependencies")] = deps;
    if (!warnings.isEmpty()) o[QStringLiteral("warnings")] = QJsonArray::fromStringList(warnings);
    if (includeFiles) {
        QJsonObject filesObj;
        for (auto it = buildFiles.cbegin(); it != buildFiles.cend(); ++it) {
            filesObj[it.key()] = it.value().left(fileChars);
        }
        o[QStringLiteral("buildFiles")] = filesObj;
    }
    return o;
}

} // namespace QindaPortage::Ops
