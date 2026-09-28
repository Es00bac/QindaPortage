// SPDX-License-Identifier: GPL-3.0-or-later
//
// PlanModel is the draft plan the assistant builds and the user edits,
// its impact preview (a virtual `emerge --pretend` with the plan applied),
// applying it through the root helper, and the transaction journal
// (docs/design.md "Changing the system").

#pragma once

#include "core/emergeplan.h"
#include "ops/executor.h"
#include "ops/plan.h"
#include "ops/pretendrunner.h"

#include <QObject>
#include <QVariantList>
#include <QVariantMap>

#include <functional>

namespace QindaPortage::App {

class SystemModel;

class PlanModel final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList steps READ steps NOTIFY planChanged)
    Q_PROPERTY(int stepCount READ stepCount NOTIFY planChanged)
    Q_PROPERTY(QString goal READ goal WRITE setGoal NOTIFY planChanged)
    Q_PROPERTY(QStringList configPreview READ configPreview NOTIFY planChanged)
    Q_PROPERTY(bool previewing READ previewing NOTIFY previewStateChanged)
    Q_PROPERTY(QString previewStage READ previewStage NOTIFY previewStateChanged)
    Q_PROPERTY(QVariantMap preview READ preview NOTIFY previewChanged)
    Q_PROPERTY(bool previewCurrent READ previewCurrent NOTIFY previewChanged)
    Q_PROPERTY(QVariantMap highlights READ highlights NOTIFY previewChanged)
    Q_PROPERTY(bool applying READ applying NOTIFY applyStateChanged)
    Q_PROPERTY(QString applyStep READ applyStep NOTIFY applyStateChanged)
    Q_PROPERTY(QStringList applyLog READ applyLog NOTIFY applyLogChanged)
    Q_PROPERTY(QVariantList journal READ journal NOTIFY journalChanged)
    Q_PROPERTY(QStringList undoCommands READ undoCommands NOTIFY applyStateChanged)
    // May the assistant apply plans itself (apply_plan)? Persisted; on by
    // default. High-risk plans need the user regardless (riskReasons).
    Q_PROPERTY(bool autonomous READ autonomous WRITE setAutonomous NOTIFY autonomyChanged)
    Q_PROPERTY(QStringList riskReasons READ riskReasons NOTIFY previewChanged)
    // The authenticated root helper session (Executor session mode): one
    // pkexec prompt, then kept until it has been idle this many minutes.
    Q_PROPERTY(bool sessionActive READ sessionActive NOTIFY sessionChanged)
    Q_PROPERTY(int sessionIdleMinutes READ sessionIdleMinutes WRITE setSessionIdleMinutes NOTIFY sessionChanged)

public:
    explicit PlanModel(QObject *parent = nullptr);
    void setSystem(const SystemModel *system) { m_system = system; }

    using ApplyDone = std::function<void(bool ok, const QString &txn, const QString &error, const QString &logTail)>;
    // Previews again when the plan changed since the last preview, refuses
    // when the preview does not resolve, then applies. `done` is called once
    // (possibly before this returns, when refused).
    // `gate`, when given, runs once the preview is current and resolved; a
    // non-empty answer refuses the apply with that reason.
    void applyVerified(const ApplyDone &done, const std::function<QString()> &gate = {});
    bool sessionActive() const { return m_executor.sessionActive(); }
    int sessionIdleMinutes() const { return m_executor.sessionIdleSeconds() / 60; }
    void setSessionIdleMinutes(int minutes);
    Q_INVOKABLE void endSession() { m_executor.endSession(); }
    bool autonomous() const { return m_autonomous; }
    void setAutonomous(bool on);
    // Why this plan needs the user's explicit approval: changes the rules in
    // docs/design.md "Safety rules" put beyond the assistant (system package
    // removal, toolchain downgrades, unmasking, live ebuilds, very large
    // merges). Empty when the assistant may apply it itself.
    QStringList riskReasons() const;

    const Ops::Plan &plan() const { return m_plan; }
    Ops::Executor *executor() { return &m_executor; }

    QVariantList steps() const;
    int stepCount() const { return m_plan.steps.size(); }
    QString goal() const { return m_plan.goal; }
    void setGoal(const QString &goal);
    QStringList configPreview() const;
    bool previewing() const { return m_runner.isRunning(); }
    QString previewStage() const { return m_stage; }
    QVariantMap preview() const { return m_preview; }
    bool previewCurrent() const { return m_previewRevision == m_revision && !m_preview.isEmpty(); }
    QVariantMap highlights() const { return m_highlights; }
    bool applying() const { return m_executor.isRunning(); }
    QString applyStep() const { return m_applyStep; }
    QStringList applyLog() const { return m_log; }
    QVariantList journal() const { return m_journal; }
    QStringList undoCommands() const { return m_undo; }

    // Adds a validated step; returns the reason when it is rejected.
    QString addStep(const Ops::PlanStep &step);
    // QML form: {kind, atom, values, rationale, path, content, options}.
    Q_INVOKABLE QString addStepMap(const QVariantMap &step);
    Q_INVOKABLE void removeStep(int index);
    Q_INVOKABLE void moveStep(int from, int to);
    Q_INVOKABLE void clear();
    Q_INVOKABLE void runPreview();
    Q_INVOKABLE bool apply();
    Q_INVOKABLE bool revert(const QString &txn);
    Q_INVOKABLE void refreshJournal();
    Q_INVOKABLE QString stepContent(int index) const;

    // Converts a resolver result for the views (full, not the compact tool
    // form).
    static QVariantMap previewMap(const Core::EmergePlan &plan);
    static QVariantMap highlightMap(const Core::EmergePlan &plan);

Q_SIGNALS:
    void planChanged();
    void previewStateChanged();
    void previewChanged();
    void applyStateChanged();
    void applyLogChanged();
    void journalChanged();
    // After an apply: ok, transaction id, error, and the last log lines.
    void applyFinished(bool ok, const QString &txn, const QString &error, const QString &logTail);
    void autonomyChanged();
    void sessionChanged();

private:
    void touch();

    void finishPendingApply(bool ok, const QString &txn, const QString &error, const QString &tail);

    const SystemModel *m_system = nullptr;
    void applyIfAllowed();

    ApplyDone m_pendingApply;   // waiting for a preview, then for the helper
    std::function<QString()> m_applyGate;
    bool m_applyAfterPreview = false;
    bool m_autonomous = true;
    Core::EmergePlan m_lastResult;
    Ops::Plan m_plan;
    Ops::PretendRunner m_runner;
    Ops::Executor m_executor;
    QVariantMap m_preview;
    QVariantMap m_highlights;
    QVariantList m_journal;
    QStringList m_log;
    QStringList m_undo;
    QString m_stage;
    QString m_applyStep;
    int m_revision = 0;
    int m_previewRevision = -1;
    int m_runningRevision = -1;
};

} // namespace QindaPortage::App
