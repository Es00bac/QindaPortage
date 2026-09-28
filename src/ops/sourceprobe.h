// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ops/sourceinfo.h"

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QUrl>

#include <functional>

class QNetworkAccessManager;

namespace QindaPortage::Ops {

// Finds out what a piece of external software is and how it builds, so an
// ebuild can be drafted for the qinda-local overlay.
//
// Sources: a GitHub repository (REST API without auth, raw file fetches),
// any other git host (shallow clone into the cache), a release tarball URL
// (download + tar), or a local directory. GitHub falls back to the clone
// path when the API refuses (rate limit: 60 requests/hour unauthenticated;
// one probe costs 4 + the number of build files).
//
// Ownership/threading: a GUI-thread QObject; all I/O is asynchronous
// (QNetworkAccessManager, QProcess). One probe at a time; probe() while busy
// cancels the previous one (its callbacks are ignored by generation).
class SourceProbe final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)

public:
    explicit SourceProbe(QObject *parent = nullptr);
    ~SourceProbe() override;

    // Clones and extracted tarballs go here (default:
    // QStandardPaths::CacheLocation + "/sources").
    void setCacheDir(const QString &dir) { m_cacheDir = dir; }
    QString cacheDir() const { return m_cacheDir; }
    // Optional shared manager (tests may inject one); owned otherwise.
    void setNetworkAccessManager(QNetworkAccessManager *nam);
    // Override the GitHub API root (tests).
    void setGithubApi(const QString &root) { m_githubApi = root; }

    bool busy() const { return m_busy; }
    const SourceInfo &result() const { return m_info; }

    Q_INVOKABLE void probe(const QString &url);
    Q_INVOKABLE void cancel();

    // Fills host/owner/repo/name/gitUrl/localPath from a URL or path.
    static bool classifyUrl(const QString &url, SourceInfo *info);
    // Reads the top-level listing and build files of a tree on disk, then
    // complete(). Synchronous; the tree is small (only named files are read).
    static void analyzeTree(const QString &dir, SourceInfo *info);
    // Derives build system, dependencies, license, name and fallbacks from
    // files/buildFiles already gathered.
    static void complete(SourceInfo *info);

Q_SIGNALS:
    void progress(const QString &message);
    void finished(const QindaPortage::Ops::SourceInfo &info);
    void busyChanged();

private:
    using Reply = std::function<void(int status, const QByteArray &body)>;

    void get(const QUrl &url, Reply done);
    void run(const QString &program, const QStringList &args, Reply done);
    bool current(quint64 generation) const { return generation == m_generation && m_busy; }
    void fail(const QString &error);
    void finish();

    // GitHub REST flow (sourceprobe_github.cpp).
    void githubRepo();
    void githubRelease();
    void githubTags();
    void githubCommit();
    void githubContents();
    void githubFiles(const QString &ref, const QStringList &names);

    // Fallback and non-GitHub paths.
    void cloneFlow(const QString &reason);
    void tarballFlow();
    void localFlow();
    void gitFacts(const QString &dir, std::function<void()> then);

    QNetworkAccessManager *m_nam = nullptr;
    bool m_ownsNam = false;
    QString m_cacheDir;
    QString m_githubApi = QStringLiteral("https://api.github.com");
    SourceInfo m_info;
    quint64 m_generation = 0;
    bool m_busy = false;
};

} // namespace QindaPortage::Ops
