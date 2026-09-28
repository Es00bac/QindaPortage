// SPDX-License-Identifier: GPL-3.0-or-later
#include "ops/sourceprobe.h"

#include "core/version.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTimer>

namespace QindaPortage::Ops {

namespace {

constexpr qint64 kMaxFileBytes = 64 * 1024;

bool isArchive(const QString &path)
{
    static const QRegularExpression re(QStringLiteral("\\.(tar\\.(gz|xz|bz2|zst)|tgz|tbz2|txz|zip)$"),
                                       QRegularExpression::CaseInsensitiveOption);
    return re.match(path).hasMatch();
}

QString snapshotVersion(const QString &isoDate)
{
    const QDateTime d = QDateTime::fromString(isoDate, Qt::ISODate);
    return QStringLiteral("0_p%1").arg((d.isValid() ? d : QDateTime::currentDateTimeUtc()).toUTC().toString(QStringLiteral("yyyyMMdd")));
}

} // namespace

SourceProbe::SourceProbe(QObject *parent)
    : QObject(parent)
    , m_cacheDir(QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + QStringLiteral("/sources"))
{
    qRegisterMetaType<QindaPortage::Ops::SourceInfo>();
}

SourceProbe::~SourceProbe() = default;

void SourceProbe::setNetworkAccessManager(QNetworkAccessManager *nam)
{
    if (m_ownsNam) delete m_nam;
    m_nam = nam;
    m_ownsNam = false;
}

bool SourceProbe::classifyUrl(const QString &input, SourceInfo *info)
{
    QString url = input.trimmed();
    info->url = url;
    if (url.startsWith(QLatin1Char('~'))) url = QDir::homePath() + url.mid(1);
    if (url.startsWith(QLatin1Char('/')) || url.startsWith(QLatin1String("file://"))) {
        info->host = QStringLiteral("local");
        info->localPath = QUrl::fromUserInput(url).toLocalFile();
        if (info->localPath.isEmpty()) info->localPath = url;
        info->repo = QFileInfo(info->localPath).fileName();
        info->name = info->repo;
        return QFileInfo(info->localPath).isDir();
    }
    const QUrl u = QUrl::fromUserInput(url);
    if (!u.isValid() || u.host().isEmpty()) return false;
    const QString path = u.path();
    if (isArchive(path)) {
        info->host = QStringLiteral("tarball");
        info->tarballUrl = u.toString();
        return true;
    }
    static const QRegularExpression forge(QStringLiteral("^/([^/]+)/([^/]+?)(?:\\.git)?(?:/.*)?$"));
    const auto m = forge.match(path);
    const QString host = u.host().toLower();
    if (m.hasMatch() && (host == QLatin1String("github.com") || host == QLatin1String("gitlab.com")
                         || host == QLatin1String("codeberg.org"))) {
        info->host = host.section(QLatin1Char('.'), 0, 0);
        info->owner = m.captured(1);
        info->repo = m.captured(2);
        info->name = info->repo;
        info->gitUrl = QStringLiteral("https://%1/%2/%3.git").arg(host, info->owner, info->repo);
        info->homepage = QStringLiteral("https://%1/%2/%3").arg(host, info->owner, info->repo);
        return true;
    }
    // Any other host: treat as a git remote.
    info->host = QStringLiteral("git");
    info->gitUrl = u.toString();
    info->repo = QFileInfo(path).fileName().remove(QRegularExpression(QStringLiteral("\\.git$")));
    info->name = info->repo;
    info->homepage = u.toString().remove(QRegularExpression(QStringLiteral("\\.git$")));
    return !info->repo.isEmpty();
}

void SourceProbe::analyzeTree(const QString &dir, SourceInfo *info)
{
    info->localPath = dir;
    QDir root(dir);
    info->files.clear();
    for (const QFileInfo &fi : root.entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden, QDir::Name)) {
        if (fi.fileName() == QLatin1String(".git")) continue;
        info->files.append(fi.isDir() ? fi.fileName() + QLatin1Char('/') : fi.fileName());
    }
    for (const QString &name : BuildFiles::interestingFiles(info->files)) {
        QFile f(root.filePath(name));
        if (f.open(QIODevice::ReadOnly)) info->buildFiles.insert(name, QString::fromUtf8(f.read(kMaxFileBytes)));
    }
    complete(info);
}

void SourceProbe::complete(SourceInfo *info)
{
    QStringList plainFiles;
    for (const QString &f : info->files) if (!f.endsWith(QLatin1Char('/'))) plainFiles.append(f);
    info->buildSystem = BuildFiles::detectBuildSystem(plainFiles);
    const QString pyproject = info->buildFiles.value(QStringLiteral("pyproject.toml"));
    if (!pyproject.isEmpty()) {
        const QString backend = BuildFiles::pep517Backend(pyproject);
        static const QStringList wrapsNative = {QStringLiteral("scikit-build-core"), QStringLiteral("meson-python"),
                                                QStringLiteral("maturin")};
        if (wrapsNative.contains(backend)) info->buildSystem = QStringLiteral("distutils-r1");
        if (info->buildSystem == QLatin1String("distutils-r1")) info->pep517Backend = backend;
        info->pythonScripts = BuildFiles::hasPythonScripts(pyproject);
    } else if (info->buildSystem == QLatin1String("distutils-r1")) {
        info->pep517Backend = QStringLiteral("setuptools");
    }
    info->dependencies.clear();
    for (auto it = info->buildFiles.cbegin(); it != info->buildFiles.cend(); ++it) {
        info->dependencies += BuildFiles::fromFile(it.key(), it.value());
    }
    info->libraryOnly = BuildFiles::declaresLibraryOnly(info->buildFiles.value(QStringLiteral("CMakeLists.txt")));

    if (info->license.isEmpty() && !info->spdxLicense.isEmpty()) info->license = BuildFiles::gentooLicense(info->spdxLicense);
    if (info->license.isEmpty()) {
        for (const char *name : {"LICENSE", "LICENSE.md", "LICENSE.txt", "COPYING"}) {
            const QString text = info->buildFiles.value(QString::fromLatin1(name));
            if (!text.isEmpty()) info->license = BuildFiles::licenseFromText(text);
            if (!info->license.isEmpty()) break;
        }
    }
    const QString cargo = info->buildFiles.value(QStringLiteral("Cargo.toml"));
    if (info->buildSystem == QLatin1String("distutils-r1")) {
        const QString pyName = BuildFiles::tomlValue(pyproject, QStringLiteral("project"), QStringLiteral("name"));
        if (!pyName.isEmpty()) info->name = pyName;
    } else if (!cargo.isEmpty()) {
        const QString crate = BuildFiles::tomlValue(cargo, QStringLiteral("package"), QStringLiteral("name"));
        if (!crate.isEmpty()) info->name = crate;
    }
    if (info->description.isEmpty()) {
        info->description = BuildFiles::tomlValue(pyproject, QStringLiteral("project"), QStringLiteral("description"));
        if (info->description.isEmpty()) {
            info->description = BuildFiles::tomlValue(cargo, QStringLiteral("package"), QStringLiteral("description"));
        }
    }
    if (info->version.isEmpty() && !info->snapshot) {
        QString declared = BuildFiles::tomlValue(pyproject, QStringLiteral("project"), QStringLiteral("version"));
        if (declared.isEmpty()) declared = BuildFiles::tomlValue(cargo, QStringLiteral("package"), QStringLiteral("version"));
        declared = BuildFiles::versionFromTag(declared);
        if (!declared.isEmpty()) info->version = declared;
    }
    if (info->buildSystem.isEmpty()) info->warnings.append(QStringLiteral("no known build system at the top level"));
    if (info->buildSystem == QLatin1String("node")) {
        info->warnings.append(QStringLiteral("npm packages need vendored node_modules to build under Portage"));
    }
}

void SourceProbe::get(const QUrl &url, Reply done)
{
    if (!m_nam) {
        m_nam = new QNetworkAccessManager(this);
        m_ownsNam = true;
    }
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("QindaPortage/0.1"));
    req.setRawHeader("Accept", "application/vnd.github+json");
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setTransferTimeout(30000);
    QNetworkReply *reply = m_nam->get(req);
    const quint64 gen = m_generation;
    connect(reply, &QNetworkReply::finished, this, [this, reply, gen, done] {
        reply->deleteLater();
        if (!current(gen)) return;
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        done(reply->error() == QNetworkReply::NoError ? status : (status ? status : -1), reply->readAll());
    });
}

void SourceProbe::run(const QString &program, const QStringList &args, Reply done)
{
    auto *p = new QProcess(this);
    p->setProcessChannelMode(QProcess::MergedChannels);
    // AGENT-GUARD: never prompt for credentials; a private repo must fail.
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("GIT_TERMINAL_PROMPT"), QStringLiteral("0"));
    p->setProcessEnvironment(env);
    const quint64 gen = m_generation;
    connect(p, &QProcess::finished, this, [this, p, gen, done](int code, QProcess::ExitStatus status) {
        p->deleteLater();
        if (!current(gen)) return;
        done(status == QProcess::NormalExit ? code : -1, p->readAll());
    });
    connect(p, &QProcess::errorOccurred, this, [this, p, gen, done](QProcess::ProcessError e) {
        if (e != QProcess::FailedToStart) return;
        p->deleteLater();
        if (current(gen)) done(-1, QByteArrayLiteral("failed to start"));
    });
    p->start(program, args);
}

void SourceProbe::probe(const QString &url)
{
    ++m_generation;
    m_info = SourceInfo{};
    const bool wasBusy = m_busy;
    m_busy = true;
    if (!wasBusy) Q_EMIT busyChanged();
    // AGENT-GUARD: always start from the event loop so finished() is never
    // emitted inside probe(); callers connect after calling it, and a
    // caller that quits its loop on finished() would otherwise hang.
    const quint64 gen = m_generation;
    QTimer::singleShot(0, this, [this, gen, url] {
        if (!current(gen)) return;
        if (!classifyUrl(url, &m_info)) {
            fail(QStringLiteral("not a repository URL, archive URL or existing directory: %1").arg(url));
            return;
        }
        Q_EMIT progress(QStringLiteral("probing %1 (%2)").arg(url, m_info.host));
        if (m_info.host == QLatin1String("github")) githubRepo();
        else if (m_info.host == QLatin1String("tarball")) tarballFlow();
        else if (m_info.host == QLatin1String("local")) localFlow();
        else cloneFlow(QString());
    });
}

void SourceProbe::cancel()
{
    ++m_generation;
    if (m_busy) {
        m_busy = false;
        Q_EMIT busyChanged();
    }
}

void SourceProbe::fail(const QString &error)
{
    m_info.error = error;
    finish();
}

void SourceProbe::finish()
{
    m_busy = false;
    Q_EMIT busyChanged();
    Q_EMIT finished(m_info);
}

void SourceProbe::gitFacts(const QString &dir, std::function<void()> then)
{
    run(QStringLiteral("git"), {QStringLiteral("-C"), dir, QStringLiteral("log"), QStringLiteral("-1"),
                                QStringLiteral("--format=%H%n%cI")},
        [this, then](int code, const QByteArray &out) {
            const QStringList lines = QString::fromUtf8(out).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
            if (code == 0 && lines.size() >= 2) {
                m_info.commit = lines.at(0).trimmed();
                m_info.commitDate = lines.at(1).trimmed();
            }
            then();
        });
}

void SourceProbe::cloneFlow(const QString &reason)
{
    if (!reason.isEmpty()) m_info.warnings.append(reason);
    Q_EMIT progress(QStringLiteral("listing tags of %1").arg(m_info.gitUrl));
    run(QStringLiteral("git"), {QStringLiteral("ls-remote"), QStringLiteral("--tags"), QStringLiteral("--refs"), m_info.gitUrl},
        [this](int code, const QByteArray &out) {
            if (code != 0) {
                fail(QStringLiteral("git ls-remote failed: %1").arg(QString::fromUtf8(out).trimmed().left(300)));
                return;
            }
            // Highest tag that reads as a version.
            Core::Version best;
            for (const QString &line : QString::fromUtf8(out).split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
                const QString tag = line.section(QStringLiteral("refs/tags/"), 1);
                const QString v = BuildFiles::versionFromTag(tag);
                const Core::Version parsed = Core::Version::parse(v);
                if (parsed.isValid() && (!best.isValid() || Core::Version::compare(parsed, best) > 0)) {
                    best = parsed;
                    m_info.tag = tag;
                    m_info.version = v;
                }
            }
            const QString dir = m_cacheDir + QLatin1Char('/') + QStringList{m_info.host, m_info.owner, m_info.repo}.join(QLatin1Char('-'));
            QDir(dir).removeRecursively();
            QDir().mkpath(m_cacheDir);
            QStringList args{QStringLiteral("clone"), QStringLiteral("--depth"), QStringLiteral("1"), QStringLiteral("--quiet")};
            if (!m_info.tag.isEmpty()) args << QStringLiteral("--branch") << m_info.tag;
            args << m_info.gitUrl << dir;
            Q_EMIT progress(QStringLiteral("cloning %1 %2").arg(m_info.gitUrl, m_info.tag));
            run(QStringLiteral("git"), args, [this, dir](int cloneCode, const QByteArray &cloneOut) {
                if (cloneCode != 0) {
                    fail(QStringLiteral("git clone failed: %1").arg(QString::fromUtf8(cloneOut).trimmed().left(300)));
                    return;
                }
                gitFacts(dir, [this, dir] {
                    if (m_info.tag.isEmpty()) {
                        m_info.snapshot = true;
                        m_info.version = snapshotVersion(m_info.commitDate);
                    }
                    const QString ref = m_info.tag.isEmpty() ? m_info.commit : m_info.tag;
                    if (m_info.host == QLatin1String("gitlab")) {
                        m_info.tarballUrl = QStringLiteral("https://gitlab.com/%1/%2/-/archive/%3/%2-%3.tar.gz")
                                                .arg(m_info.owner, m_info.repo, ref);
                        m_info.archiveDir = m_info.repo + QLatin1Char('-') + ref;
                    } else if (m_info.host == QLatin1String("codeberg")) {
                        m_info.tarballUrl = QStringLiteral("https://codeberg.org/%1/%2/archive/%3.tar.gz")
                                                .arg(m_info.owner, m_info.repo, ref);
                        m_info.archiveDir = m_info.repo;
                    }
                    analyzeTree(dir, &m_info);
                    finish();
                });
            });
        });
}

void SourceProbe::tarballFlow()
{
    const QString fileName = QUrl(m_info.tarballUrl).fileName();
    static const QRegularExpression nameVer(QStringLiteral("^(.+?)-v?(\\d[\\w.]*?)\\.(?:tar|tgz|tbz2|txz|zip)"));
    if (const auto m = nameVer.match(fileName); m.hasMatch()) {
        m_info.repo = m.captured(1);
        m_info.name = m.captured(1);
        m_info.version = BuildFiles::versionFromTag(m.captured(2));
    }
    Q_EMIT progress(QStringLiteral("downloading %1").arg(fileName));
    get(QUrl(m_info.tarballUrl), [this, fileName](int status, const QByteArray &body) {
        if (status < 200 || status >= 300 || body.isEmpty()) {
            fail(QStringLiteral("download failed (HTTP %1)").arg(status));
            return;
        }
        const QString base = m_cacheDir + QStringLiteral("/tarballs");
        const QString extract = base + QLatin1Char('/') + fileName + QStringLiteral(".d");
        QDir(extract).removeRecursively();
        QDir().mkpath(extract);
        QFile f(base + QLatin1Char('/') + fileName);
        if (!f.open(QIODevice::WriteOnly) || f.write(body) != body.size()) {
            fail(QStringLiteral("cannot write %1").arg(f.fileName()));
            return;
        }
        f.close();
        if (fileName.endsWith(QLatin1String(".zip"), Qt::CaseInsensitive)) {
            fail(QStringLiteral("zip archives are not supported; use the project's .tar.* release"));
            return;
        }
        run(QStringLiteral("tar"), {QStringLiteral("-xf"), f.fileName(), QStringLiteral("-C"), extract},
            [this, extract](int code, const QByteArray &out) {
                if (code != 0) {
                    fail(QStringLiteral("tar failed: %1").arg(QString::fromUtf8(out).left(300)));
                    return;
                }
                const QStringList top = QDir(extract).entryList(QDir::AllEntries | QDir::NoDotAndDotDot);
                QString root = extract;
                if (top.size() == 1 && QFileInfo(extract + QLatin1Char('/') + top.first()).isDir()) {
                    m_info.archiveDir = top.first();
                    root += QLatin1Char('/') + top.first();
                }
                analyzeTree(root, &m_info);
                if (m_info.version.isEmpty()) m_info.warnings.append(QStringLiteral("no version in the archive name"));
                finish();
            });
    });
}

void SourceProbe::localFlow()
{
    const QString dir = m_info.localPath;
    if (!QFileInfo(dir + QStringLiteral("/.git")).exists()) {
        m_info.warnings.append(QStringLiteral("not a git checkout: the plan must package it as a tarball"));
        analyzeTree(dir, &m_info);
        finish();
        return;
    }
    m_info.gitUrl = QStringLiteral("file://") + QFileInfo(dir).canonicalFilePath();
    run(QStringLiteral("git"), {QStringLiteral("-C"), dir, QStringLiteral("describe"), QStringLiteral("--tags"),
                                QStringLiteral("--exact-match")},
        [this, dir](int code, const QByteArray &out) {
            if (code == 0) {
                m_info.tag = QString::fromUtf8(out).trimmed();
                m_info.version = BuildFiles::versionFromTag(m_info.tag);
            }
            gitFacts(dir, [this, dir] {
                if (m_info.version.isEmpty()) {
                    m_info.snapshot = true;
                    m_info.tag.clear();
                    m_info.version = snapshotVersion(m_info.commitDate);
                }
                analyzeTree(dir, &m_info);
                finish();
            });
        });
}

} // namespace QindaPortage::Ops
