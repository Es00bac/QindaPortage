// SPDX-License-Identifier: GPL-3.0-or-later
//
// SourceProbe: the GitHub REST flow. repo -> latest release (or newest tag,
// or default-branch commit) -> top-level contents -> raw build files.
// Any API refusal (rate limit, 404 on a renamed repo) falls back to the
// clone flow in sourceprobe.cpp.
#include "ops/sourceprobe.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSharedPointer>

namespace QindaPortage::Ops {

namespace {

QJsonObject object(const QByteArray &body)
{
    return QJsonDocument::fromJson(body).object();
}

QString repoPath(const SourceInfo &info)
{
    return QStringLiteral("/repos/%1/%2").arg(info.owner, info.repo);
}

} // namespace

void SourceProbe::githubRepo()
{
    Q_EMIT progress(QStringLiteral("reading github.com/%1/%2").arg(m_info.owner, m_info.repo));
    get(QUrl(m_githubApi + repoPath(m_info)), [this](int status, const QByteArray &body) {
        if (status != 200) {
            cloneFlow(QStringLiteral("GitHub API answered %1; used git instead").arg(status));
            return;
        }
        const QJsonObject o = object(body);
        m_info.description = o.value(QStringLiteral("description")).toString();
        const QString homepage = o.value(QStringLiteral("homepage")).toString();
        if (!homepage.isEmpty()) m_info.homepage = homepage;
        m_info.language = o.value(QStringLiteral("language")).toString();
        m_info.defaultBranch = o.value(QStringLiteral("default_branch")).toString();
        m_info.spdxLicense = o.value(QStringLiteral("license")).toObject().value(QStringLiteral("spdx_id")).toString();
        if (m_info.spdxLicense == QLatin1String("NOASSERTION")) m_info.spdxLicense.clear();
        for (const QJsonValue &t : o.value(QStringLiteral("topics")).toArray()) m_info.topics.append(t.toString());
        // The API reports renames; follow the canonical names.
        const QString fullName = o.value(QStringLiteral("full_name")).toString();
        if (fullName.contains(QLatin1Char('/'))) {
            m_info.owner = fullName.section(QLatin1Char('/'), 0, 0);
            m_info.repo = fullName.section(QLatin1Char('/'), 1);
        }
        if (o.value(QStringLiteral("archived")).toBool()) m_info.warnings.append(QStringLiteral("the repository is archived"));
        githubRelease();
    });
}

void SourceProbe::githubRelease()
{
    get(QUrl(m_githubApi + repoPath(m_info) + QStringLiteral("/releases/latest")), [this](int status, const QByteArray &body) {
        if (status == 200) {
            const QString tag = object(body).value(QStringLiteral("tag_name")).toString();
            const QString version = BuildFiles::versionFromTag(tag);
            if (!version.isEmpty()) {
                m_info.tag = tag;
                m_info.version = version;
                githubContents();
                return;
            }
            m_info.warnings.append(QStringLiteral("latest release tag '%1' is not a version").arg(tag));
        }
        githubTags();
    });
}

void SourceProbe::githubTags()
{
    get(QUrl(m_githubApi + repoPath(m_info) + QStringLiteral("/tags?per_page=30")), [this](int status, const QByteArray &body) {
        if (status == 200) {
            // Tags come newest-created first; take the first that is a version.
            for (const QJsonValue &v : QJsonDocument::fromJson(body).array()) {
                const QString tag = v.toObject().value(QStringLiteral("name")).toString();
                const QString version = BuildFiles::versionFromTag(tag);
                if (version.isEmpty()) continue;
                m_info.tag = tag;
                m_info.version = version;
                m_info.commit = v.toObject().value(QStringLiteral("commit")).toObject().value(QStringLiteral("sha")).toString();
                githubContents();
                return;
            }
        }
        githubCommit();
    });
}

void SourceProbe::githubCommit()
{
    const QString branch = m_info.defaultBranch.isEmpty() ? QStringLiteral("HEAD") : m_info.defaultBranch;
    get(QUrl(m_githubApi + repoPath(m_info) + QStringLiteral("/commits/") + branch), [this](int status, const QByteArray &body) {
        if (status != 200) {
            cloneFlow(QStringLiteral("GitHub API answered %1 for the latest commit; used git instead").arg(status));
            return;
        }
        const QJsonObject o = object(body);
        m_info.commit = o.value(QStringLiteral("sha")).toString();
        m_info.commitDate = o.value(QStringLiteral("commit")).toObject().value(QStringLiteral("committer"))
                                .toObject().value(QStringLiteral("date")).toString();
        m_info.snapshot = true;
        const QDateTime d = QDateTime::fromString(m_info.commitDate, Qt::ISODate);
        m_info.version = QStringLiteral("0_p%1").arg((d.isValid() ? d : QDateTime::currentDateTimeUtc())
                                                         .toUTC().toString(QStringLiteral("yyyyMMdd")));
        m_info.warnings.append(QStringLiteral("no release or version tag: pinned to commit %1").arg(m_info.commit.left(12)));
        githubContents();
    });
}

void SourceProbe::githubContents()
{
    const QString ref = m_info.snapshot ? m_info.commit : m_info.tag;
    get(QUrl(m_githubApi + repoPath(m_info) + QStringLiteral("/contents?ref=") + ref),
        [this, ref](int status, const QByteArray &body) {
            if (status != 200) {
                cloneFlow(QStringLiteral("GitHub API answered %1 for the file list; used git instead").arg(status));
                return;
            }
            m_info.files.clear();
            for (const QJsonValue &v : QJsonDocument::fromJson(body).array()) {
                const QJsonObject e = v.toObject();
                const QString name = e.value(QStringLiteral("name")).toString();
                m_info.files.append(e.value(QStringLiteral("type")).toString() == QLatin1String("dir")
                                        ? name + QLatin1Char('/') : name);
            }
            githubFiles(ref, BuildFiles::interestingFiles(m_info.files));
        });
}

void SourceProbe::githubFiles(const QString &ref, const QStringList &names)
{
    auto finalize = [this] {
        if (m_info.snapshot) {
            m_info.tarballUrl = QStringLiteral("https://github.com/%1/%2/archive/%3.tar.gz")
                                    .arg(m_info.owner, m_info.repo, m_info.commit);
            m_info.archiveDir = m_info.repo + QLatin1Char('-') + m_info.commit;
        } else {
            m_info.tarballUrl = QStringLiteral("https://github.com/%1/%2/archive/refs/tags/%3.tar.gz")
                                    .arg(m_info.owner, m_info.repo, m_info.tag);
            // GitHub drops a leading "v" from the archive's directory name.
            const QString dirTag = m_info.tag.startsWith(QLatin1Char('v')) && m_info.tag.size() > 1 && m_info.tag.at(1).isDigit()
                ? m_info.tag.mid(1) : m_info.tag;
            m_info.archiveDir = m_info.repo + QLatin1Char('-') + dirTag;
        }
        complete(&m_info);
        finish();
    };
    if (names.isEmpty()) {
        finalize();
        return;
    }
    Q_EMIT progress(QStringLiteral("reading %1 build file(s) at %2").arg(names.size()).arg(ref));
    auto pending = QSharedPointer<int>::create(int(names.size()));
    for (const QString &name : names) {
        const QUrl url(QStringLiteral("https://raw.githubusercontent.com/%1/%2/%3/%4").arg(m_info.owner, m_info.repo, ref, name));
        get(url, [this, name, pending, finalize](int status, const QByteArray &body) {
            if (status == 200) m_info.buildFiles.insert(name, QString::fromUtf8(body.left(name == QLatin1String("Cargo.lock") ? 4 * 1024 * 1024 : 64 * 1024)));
            else m_info.warnings.append(QStringLiteral("could not read %1 (HTTP %2)").arg(name).arg(status));
            if (--*pending == 0) finalize();
        });
    }
}

} // namespace QindaPortage::Ops
