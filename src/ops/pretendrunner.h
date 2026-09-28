// SPDX-License-Identifier: GPL-3.0-or-later
//
// Runs `emerge --pretend` as the user and parses the result. With a Plan,
// the plan's configuration and ebuilds are applied *virtually*: a scratch
// PORTAGE_CONFIGROOT holds a copy of /etc/portage with the plan's fragments
// appended, and a scratch copy of the qinda-local overlay holds its ebuilds.
// Nothing outside the scratch directory is written.
//
// AGENT-NOTE: verified on qinda-top (2026-09-28) as the unprivileged user:
// PORTAGE_CONFIGROOT=<scratch> with "app-misc/jq static-libs" appended made
// `emerge -p app-misc/jq` resolve with static-libs on and surface the
// follow-on requirement (dev-libs/oniguruma[static-libs]); ROOT and the
// installed database stay "/", and the binhost index is still read.

#pragma once

#include "core/emergeplan.h"
#include "ops/plan.h"

#include <QObject>
#include <QProcess>
#include <QStringList>

namespace QindaPortage::Ops {

class PretendRunner final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool running READ isRunning NOTIFY runningChanged)

public:
    explicit PretendRunner(QObject *parent = nullptr);
    ~PretendRunner() override;

    // Test seams; defaults are "emerge" and "ebuild" from PATH.
    void setEmergeProgram(const QString &program, const QStringList &prefixArgs = {});
    void setEbuildProgram(const QString &program, const QStringList &prefixArgs = {});
    // Defaults: /etc/portage, /var/db/repos/qinda-local and
    // <cache>/qindaportage/pretend.
    void setSourceConfigDir(const QString &dir) { m_sourceEtc = dir; }
    void setSourceOverlayDir(const QString &dir) { m_sourceOverlay = dir; }
    void setScratchDir(const QString &dir) { m_scratch = dir; }
    QString scratchDir() const { return m_scratch; }

    // Resolves `targets` with `options`, applying `plan` virtually when it
    // has config or ebuild steps. Returns false (and emits nothing) when
    // already running or the plan does not validate.
    bool start(const QStringList &targets, const QStringList &options = {}, const Plan &plan = Plan());
    void cancel();
    bool isRunning() const { return m_process != nullptr; }

    const Core::EmergePlan &result() const { return m_result; }
    QString rawOutput() const { return m_output; }
    int exitCode() const { return m_exitCode; }
    QString error() const { return m_error; }

    // Builds `<configRoot>/etc/portage` from `sourceEtc` plus the plan's
    // fragments, and, when the plan writes ebuilds, `overlayDir` from
    // `sourceOverlay` plus those files with a repos.conf entry pointing at
    // it. Returns the ebuild files that need a Manifest. Exposed for tests.
    static bool prepareConfigRoot(const QString &sourceEtc, const QString &configRoot,
                                  const QString &sourceOverlay, const QString &overlayDir,
                                  const Plan &plan, QStringList *ebuildFiles, QString *error);
    static QStringList emergeArguments(const QStringList &targets, const QStringList &options);
    // Where a package.<kind> fragment goes under `etcDir`: the
    // 90-qindaportage file inside a directory form, else the file itself.
    static QString managedFile(const QString &etcDir, const QString &configFile);

Q_SIGNALS:
    void runningChanged();
    // "preparing", "manifest <file>", "resolving"
    void stageChanged(const QString &stage);
    void finished(bool ok, const QString &error);

private:
    void runNext();
    void launch(const QString &program, const QStringList &args, const QStringList &env);
    void processFinished(int exitCode, QProcess::ExitStatus status);
    void finish(bool ok, const QString &error);

    QString m_emerge = QStringLiteral("emerge");
    QStringList m_emergePrefix;
    QString m_ebuild = QStringLiteral("ebuild");
    QStringList m_ebuildPrefix;
    QString m_sourceEtc = QStringLiteral("/etc/portage");
    QString m_sourceOverlay = QStringLiteral("/var/db/repos/qinda-local");
    QString m_scratch;

    QProcess *m_process = nullptr;
    QStringList m_pendingManifests;
    QStringList m_targets;
    QStringList m_options;
    QString m_configRoot; // empty: run against the real configuration
    bool m_resolving = false;
    Core::EmergePlan m_result;
    QString m_output;
    QString m_error;
    int m_exitCode = -1;
};

} // namespace QindaPortage::Ops
