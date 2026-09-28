// SPDX-License-Identifier: GPL-3.0-or-later
#include "ops/pretendrunner.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcessEnvironment>
#include <QStandardPaths>

namespace QindaPortage::Ops {

namespace {

// Copies a directory tree without following symlinks. Relative symlinks
// are rewritten to absolute targets so they resolve from the new place
// (make.profile is "../../var/db/repos/..."). Unreadable files are skipped:
// the copy is for resolving, and Portage reads nothing secret from them.
bool copyTree(const QString &src, const QString &dst, const QStringList &linkInstead, QString *error)
{
    if (!QDir().mkpath(dst)) {
        *error = QStringLiteral("cannot create %1").arg(dst);
        return false;
    }
    QDir dir(src);
    const auto entries = dir.entryInfoList(QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot);
    for (const QFileInfo &info : entries) {
        const QString target = dst + QLatin1Char('/') + info.fileName();
        if (info.isSymLink()) {
            // symLinkTarget() is absolute, resolved against the link's directory.
            if (!QFile::link(info.symLinkTarget(), target)) {
                *error = QStringLiteral("cannot link %1").arg(target);
                return false;
            }
        } else if (linkInstead.contains(info.fileName())) {
            QFile::link(info.absoluteFilePath(), target);
        } else if (info.isDir()) {
            if (!info.isReadable() || !info.isExecutable()) continue;
            if (!copyTree(info.absoluteFilePath(), target, {}, error)) return false;
        } else if (info.isReadable()) {
            QFile::copy(info.absoluteFilePath(), target);
            QFile::setPermissions(target, QFile::permissions(target) | QFileDevice::WriteOwner);
        }
    }
    return true;
}

bool appendText(const QString &path, const QString &text, QString *error)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Append)) {
        *error = QStringLiteral("cannot write %1").arg(path);
        return false;
    }
    f.write(text.toUtf8());
    return true;
}

bool writeText(const QString &path, const QString &text, QString *error)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        *error = QStringLiteral("cannot write %1").arg(path);
        return false;
    }
    f.write(text.toUtf8());
    return true;
}

} // namespace

PretendRunner::PretendRunner(QObject *parent)
    : QObject(parent)
{
    m_scratch = QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation)
        + QStringLiteral("/qindaportage/pretend");
}

PretendRunner::~PretendRunner()
{
    cancel();
}

void PretendRunner::setEmergeProgram(const QString &program, const QStringList &prefixArgs)
{
    m_emerge = program;
    m_emergePrefix = prefixArgs;
}

void PretendRunner::setEbuildProgram(const QString &program, const QStringList &prefixArgs)
{
    m_ebuild = program;
    m_ebuildPrefix = prefixArgs;
}

QStringList PretendRunner::emergeArguments(const QStringList &targets, const QStringList &options)
{
    QStringList args{QStringLiteral("--pretend"), QStringLiteral("--verbose"), QStringLiteral("--tree"),
                     QStringLiteral("--color=n"), QStringLiteral("--nospinner")};
    for (const QString &o : options) {
        if (!args.contains(o) && o != QLatin1String("--oneshot") && o != QLatin1String("--fetchonly")) args.append(o);
    }
    args += targets;
    return args;
}

QString PretendRunner::managedFile(const QString &etcDir, const QString &configFile)
{
    const QString base = etcDir + QLatin1Char('/') + configFile;
    QFileInfo info(base);
    // AGENT-CONTRACT: same placement as helper/qindaportage-apply
    // (managed_file()): directory form gets 90-qindaportage, a single file
    // is appended to, a missing path becomes the directory form.
    if (info.exists() && !info.isDir()) return base;
    return base + QStringLiteral("/90-qindaportage");
}

bool PretendRunner::prepareConfigRoot(const QString &sourceEtc, const QString &configRoot,
                                      const QString &sourceOverlay, const QString &overlayDir,
                                      const Plan &plan, QStringList *ebuildFiles, QString *error)
{
    QDir(configRoot).removeRecursively();
    const QString etc = configRoot + QStringLiteral("/etc/portage");
    // gnupg holds the binpkg keyring (partly root-only); link rather than copy.
    if (!copyTree(sourceEtc, etc, {QStringLiteral("gnupg")}, error)) return false;

    const QMap<QString, QString> fragments = plan.configFragments();
    for (auto it = fragments.cbegin(); it != fragments.cend(); ++it) {
        if (!appendText(managedFile(etc, it.key()), it.value(), error)) return false;
    }

    if (!plan.writesEbuilds()) return true;
    QDir(overlayDir).removeRecursively();
    if (QFileInfo(sourceOverlay).isDir()) {
        if (!copyTree(sourceOverlay, overlayDir, {QStringLiteral(".git")}, error)) return false;
    }
    if (!QFileInfo::exists(overlayDir + QStringLiteral("/metadata/layout.conf"))) {
        if (!writeText(overlayDir + QStringLiteral("/metadata/layout.conf"),
                       QStringLiteral("masters = gentoo\nthin-manifests = true\nsign-manifests = false\n"), error)) {
            return false;
        }
    }
    if (!writeText(overlayDir + QStringLiteral("/profiles/repo_name"), QStringLiteral("qinda-local\n"), error)) {
        return false;
    }
    QStringList categories;
    {
        QFile f(overlayDir + QStringLiteral("/profiles/categories"));
        if (f.open(QIODevice::ReadOnly)) {
            categories = QString::fromUtf8(f.readAll()).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        }
    }
    for (const PlanStep &s : plan.steps) {
        if (s.kind != PlanStep::WriteEbuild) continue;
        const QString file = overlayDir + QLatin1Char('/') + s.path;
        if (!writeText(file, s.content, error)) return false;
        const QString category = s.path.section(QLatin1Char('/'), 0, 0);
        if (!categories.contains(category)) categories.append(category);
        if (s.path.endsWith(QLatin1String(".ebuild"))) ebuildFiles->append(file);
    }
    categories.sort();
    if (!writeText(overlayDir + QStringLiteral("/profiles/categories"),
                   categories.join(QLatin1Char('\n')) + QLatin1Char('\n'), error)) {
        return false;
    }

    // repos.conf: a later file's [qinda-local] section overrides an existing
    // one (Portage merges repos.conf files in order), so the scratch overlay
    // wins. A single-file repos.conf is turned into the directory form.
    const QString repos = etc + QStringLiteral("/repos.conf");
    if (QFileInfo(repos).isFile()) {
        const QString moved = configRoot + QStringLiteral("/repos.conf.orig");
        QFile::rename(repos, moved);
        QDir().mkpath(repos);
        QFile::rename(moved, repos + QStringLiteral("/00-repos.conf"));
    }
    return writeText(repos + QStringLiteral("/zz-qinda-local-pretend.conf"),
                     QStringLiteral("[qinda-local]\nlocation = %1\npriority = 50\nauto-sync = no\n").arg(overlayDir),
                     error);
}

bool PretendRunner::start(const QStringList &targets, const QStringList &options, const Plan &plan)
{
    if (m_process) return false;
    m_error.clear();
    m_output.clear();
    m_exitCode = -1;
    m_result = {};
    m_targets = targets;
    m_options = options;
    m_pendingManifests.clear();
    m_configRoot.clear();
    m_resolving = false;

    bool virtualPlan = false;
    for (const PlanStep &s : plan.steps) virtualPlan |= s.isConfigChange() || s.kind == PlanStep::WriteEbuild;
    if (virtualPlan) {
        QString why;
        if (!plan.validate(&why)) {
            m_error = why;
            return false;
        }
        Q_EMIT stageChanged(QStringLiteral("preparing"));
        m_configRoot = m_scratch + QStringLiteral("/root");
        if (!prepareConfigRoot(m_sourceEtc, m_configRoot, m_sourceOverlay, m_scratch + QStringLiteral("/overlay"),
                               plan, &m_pendingManifests, &m_error)) {
            return false;
        }
        QDir().mkpath(m_scratch + QStringLiteral("/distfiles"));
        QDir().mkpath(m_scratch + QStringLiteral("/tmp"));
    }
    runNext();
    Q_EMIT runningChanged();
    return m_process != nullptr;
}

void PretendRunner::runNext()
{
    QStringList env;
    if (!m_configRoot.isEmpty()) env.append(QStringLiteral("PORTAGE_CONFIGROOT=") + m_configRoot);
    if (!m_pendingManifests.isEmpty()) {
        const QString file = m_pendingManifests.takeFirst();
        Q_EMIT stageChanged(QStringLiteral("manifest ") + QFileInfo(file).fileName());
        // Sources already in the system DISTDIR are reused read-only; new
        // ones are fetched into the scratch directory, never the real one.
        env << QStringLiteral("DISTDIR=") + m_scratch + QStringLiteral("/distfiles")
            << QStringLiteral("PORTAGE_RO_DISTDIRS=/var/cache/distfiles")
            << QStringLiteral("PORTAGE_TMPDIR=") + m_scratch + QStringLiteral("/tmp");
        launch(m_ebuild, m_ebuildPrefix + QStringList{file, QStringLiteral("manifest")}, env);
        return;
    }
    m_resolving = true;
    Q_EMIT stageChanged(QStringLiteral("resolving"));
    launch(m_emerge, m_emergePrefix + emergeArguments(m_targets, m_options), env);
}

void PretendRunner::launch(const QString &program, const QStringList &args, const QStringList &env)
{
    delete m_process;
    m_process = new QProcess(this);
    m_process->setProcessChannelMode(QProcess::MergedChannels);
    QProcessEnvironment e = QProcessEnvironment::systemEnvironment();
    e.insert(QStringLiteral("NOCOLOR"), QStringLiteral("true"));
    for (const QString &kv : env) e.insert(kv.section(QLatin1Char('='), 0, 0), kv.section(QLatin1Char('='), 1));
    m_process->setProcessEnvironment(e);
    connect(m_process, &QProcess::finished, this, &PretendRunner::processFinished);
    connect(m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError err) {
        if (err == QProcess::FailedToStart) finish(false, QStringLiteral("cannot start %1").arg(m_process->program()));
    });
    m_process->start(program, args);
}

void PretendRunner::processFinished(int exitCode, QProcess::ExitStatus status)
{
    if (!m_process) return;
    const QString out = QString::fromUtf8(m_process->readAll());
    if (!m_resolving) {
        if (status != QProcess::NormalExit || exitCode != 0) {
            m_output = out;
            finish(false, QStringLiteral("Manifest failed:\n%1").arg(out.right(4000)));
            return;
        }
        runNext();
        return;
    }
    m_output = out;
    m_exitCode = exitCode;
    m_result = Core::EmergePlanParser::parse(out);
    // emerge exits non-zero when it cannot resolve; the parsed problems say
    // why, so that is still a successful *pretend*.
    finish(status == QProcess::NormalExit, status == QProcess::NormalExit ? QString() : QStringLiteral("emerge crashed"));
}

void PretendRunner::finish(bool ok, const QString &error)
{
    m_error = error;
    if (m_process) {
        m_process->disconnect(this);
        m_process->deleteLater();
        m_process = nullptr;
    }
    Q_EMIT runningChanged();
    Q_EMIT finished(ok, error);
}

void PretendRunner::cancel()
{
    if (!m_process) return;
    m_process->disconnect(this);
    m_process->kill();
    m_process->waitForFinished(2000);
    delete m_process;
    m_process = nullptr;
    m_error = QStringLiteral("cancelled");
    Q_EMIT runningChanged();
}

} // namespace QindaPortage::Ops
