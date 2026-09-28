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

namespace QindaPortage::App {

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

public:
    explicit PlanModel(QObject *parent = nullptr);

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

private:
    void touch();

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
