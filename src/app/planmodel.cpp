// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/planmodel.h"

#include <QDir>
#include <QJsonArray>

namespace QindaPortage::App {

using namespace Ops;

namespace {

QString sourceTree()
{
#ifdef QINDAPORTAGE_SOURCE_DIR
    return QStringLiteral(QINDAPORTAGE_SOURCE_DIR);
#else
    return QString();
#endif
}

} // namespace

PlanModel::PlanModel(QObject *parent)
    : QObject(parent)
{
    m_executor.setCommand(Executor::defaultCommand(sourceTree()));
    connect(&m_runner, &PretendRunner::stageChanged, this, [this](const QString &stage) {
        m_stage = stage;
        Q_EMIT previewStateChanged();
    });
    connect(&m_runner, &PretendRunner::runningChanged, this, &PlanModel::previewStateChanged);
    connect(&m_runner, &PretendRunner::finished, this, [this](bool ok, const QString &error) {
        m_stage.clear();
        if (!ok) {
            m_preview = {{QStringLiteral("error"), error}};
            m_highlights.clear();
        } else {
            m_preview = previewMap(m_runner.result());
            m_highlights = highlightMap(m_runner.result());
        }
        m_previewRevision = m_runningRevision;
        Q_EMIT previewStateChanged();
        Q_EMIT previewChanged();
    });

    connect(&m_executor, &Executor::runningChanged, this, &PlanModel::applyStateChanged);
    connect(&m_executor, &Executor::stepStarted, this, [this](int index, const QString &text) {
        m_applyStep = QStringLiteral("%1/%2 %3").arg(index + 1).arg(m_plan.steps.size()).arg(text);
        Q_EMIT applyStateChanged();
    });
    connect(&m_executor, &Executor::logLine, this, [this](const QString &line) {
        m_log.append(line);
        if (m_log.size() > 2000) m_log.remove(0, m_log.size() - 2000);
        Q_EMIT applyLogChanged();
    });
    connect(&m_executor, &Executor::undoSuggested, this, [this](const QStringList &commands) {
        m_undo = commands;
        Q_EMIT applyStateChanged();
    });
    connect(&m_executor, &Executor::finished, this,
            [this](bool ok, const QString &txn, const QString &error) {
                m_applyStep = ok ? QStringLiteral("done") : QStringLiteral("failed");
                const QString tail = m_log.mid(std::max<qsizetype>(0, m_log.size() - 80)).join(QLatin1Char('\n'));
                Q_EMIT applyStateChanged();
                refreshJournal();
                if (ok) clear();
                Q_EMIT applyFinished(ok, txn, error, tail);
            });
    refreshJournal();
}

void PlanModel::touch()
{
    ++m_revision;
    Q_EMIT planChanged();
    Q_EMIT previewChanged(); // previewCurrent depends on the revision
}

QVariantList PlanModel::steps() const
{
    QVariantList out;
    for (const PlanStep &s : m_plan.steps) {
        out.append(QVariantMap{{QStringLiteral("kind"), PlanStep::kindName(s.kind)},
                               {QStringLiteral("atom"), s.atom},
                               {QStringLiteral("values"), s.values},
                               {QStringLiteral("options"), s.options},
                               {QStringLiteral("path"), s.path},
                               {QStringLiteral("rationale"), s.rationale},
                               {QStringLiteral("text"), s.describe()},
                               {QStringLiteral("hasContent"), !s.content.isEmpty()},
                               {QStringLiteral("build"), s.isBuild()}});
    }
    return out;
}

QString PlanModel::stepContent(int index) const
{
    return index >= 0 && index < m_plan.steps.size() ? m_plan.steps.at(index).content : QString();
}

void PlanModel::setGoal(const QString &goal)
{
    if (goal == m_plan.goal) return;
    m_plan.goal = goal;
    if (m_plan.title.isEmpty()) m_plan.title = goal.left(80);
    Q_EMIT planChanged();
}

QStringList PlanModel::configPreview() const
{
    QStringList out;
    const auto fragments = m_plan.configFragments();
    for (auto it = fragments.cbegin(); it != fragments.cend(); ++it) {
        out.append(QStringLiteral("# /etc/portage/%1/90-qindaportage\n%2").arg(it.key(), it.value().trimmed()));
    }
    return out;
}

QString PlanModel::addStep(const PlanStep &step)
{
    const QString why = step.validate();
    if (!why.isEmpty()) return why;
    // The same config line twice is noise; keep the first.
    for (const PlanStep &existing : m_plan.steps) {
        if (existing.kind == step.kind && existing.atom == step.atom && existing.values == step.values
            && existing.path == step.path && existing.content == step.content) {
            return {};
        }
    }
    // Config changes go before builds so the helper writes them first.
    int at = m_plan.steps.size();
    if (step.isConfigChange() || step.kind == PlanStep::AddPatch || step.kind == PlanStep::WriteEbuild) {
        for (int i = 0; i < m_plan.steps.size(); ++i) {
            if (m_plan.steps.at(i).isBuild()) { at = i; break; }
        }
    }
    m_plan.steps.insert(at, step);
    touch();
    return {};
}

QString PlanModel::addStepMap(const QVariantMap &map)
{
    bool ok = false;
    const PlanStep step = PlanStep::fromJson(QJsonObject::fromVariantMap(map), &ok);
    if (!ok) return QStringLiteral("unknown step kind %1").arg(map.value(QStringLiteral("kind")).toString());
    return addStep(step);
}

void PlanModel::removeStep(int index)
{
    if (index < 0 || index >= m_plan.steps.size()) return;
    m_plan.steps.removeAt(index);
    touch();
}

void PlanModel::moveStep(int from, int to)
{
    if (from < 0 || to < 0 || from >= m_plan.steps.size() || to >= m_plan.steps.size() || from == to) return;
    m_plan.steps.move(from, to);
    touch();
}

void PlanModel::clear()
{
    m_plan = Plan();
    m_preview.clear();
    m_highlights.clear();
    touch();
}

void PlanModel::runPreview()
{
    if (m_runner.isRunning()) m_runner.cancel();
    QStringList targets = m_plan.emergeTargets();
    QStringList options = m_plan.emergeOptions();
    if (targets.isEmpty()) {
        // Config-only plans: what would the next world update do with them?
        targets = {QStringLiteral("@world")};
        options = {QStringLiteral("--update"), QStringLiteral("--deep"), QStringLiteral("--newuse")};
    }
    m_runningRevision = m_revision;
    if (!m_runner.start(targets, options, m_plan)) {
        m_preview = {{QStringLiteral("error"), m_runner.error()}};
        Q_EMIT previewChanged();
    }
    Q_EMIT previewStateChanged();
}

bool PlanModel::apply()
{
    if (m_executor.isRunning() || m_plan.isEmpty()) return false;
    if (m_plan.id.isEmpty()) m_plan.id = Plan::newId();
    if (m_plan.title.isEmpty()) m_plan.title = m_plan.goal.isEmpty() ? QStringLiteral("QindaPortage plan") : m_plan.goal.left(80);
    m_log.clear();
    m_undo.clear();
    Q_EMIT applyLogChanged();
    QString error;
    // AGENT-CONTRACT: Executor::aboutToStart (connected to LlmRuntime::unload
    // in main.cpp) fires synchronously inside start().
    if (!m_executor.start(m_plan, &error)) {
        m_log.append(QStringLiteral("could not start the helper: %1").arg(error));
        Q_EMIT applyLogChanged();
        return false;
    }
    return true;
}

bool PlanModel::revert(const QString &txn)
{
    if (m_executor.isRunning()) return false;
    m_log.clear();
    Q_EMIT applyLogChanged();
    return m_executor.revert(txn);
}

void PlanModel::refreshJournal()
{
    m_journal.clear();
    for (const QJsonObject &entry : Executor::journal()) m_journal.append(entry.toVariantMap());
    Q_EMIT journalChanged();
}

QVariantMap PlanModel::previewMap(const Core::EmergePlan &plan)
{
    QVariantList merges;
    for (const Core::MergeItem &it : plan.items) {
        if (it.action == Core::MergeItem::NoMerge) continue;
        merges.append(QVariantMap{
            {QStringLiteral("action"), Core::MergeItem::actionName(it.action)},
            {QStringLiteral("key"), it.key}, {QStringLiteral("version"), it.version},
            {QStringLiteral("from"), it.oldVersion}, {QStringLiteral("slot"), it.slot},
            {QStringLiteral("repo"), it.repository}, {QStringLiteral("binary"), it.binary},
            {QStringLiteral("keyword"), it.keywordMasked},
            {QStringLiteral("useChanges"), it.changedFlags().join(QLatin1Char(' '))},
            {QStringLiteral("download"), it.downloadKiB > 0 ? QStringLiteral("%1 KiB").arg(it.downloadKiB) : QString()},
            {QStringLiteral("blocker"), it.blocker}, {QStringLiteral("depth"), it.depth}});
    }
    QVariantList problems;
    for (const Core::Problem &p : plan.problems) {
        QVariantList fixes;
        for (const Core::Fix &f : p.fixes) {
            fixes.append(QVariantMap{{QStringLiteral("kind"), Core::Fix::kindName(f.kind)},
                                     {QStringLiteral("atom"), f.atom},
                                     {QStringLiteral("values"), f.values.join(QLatin1Char(' '))},
                                     {QStringLiteral("reason"), f.reason}});
        }
        problems.append(QVariantMap{{QStringLiteral("kind"), Core::Problem::kindName(p.kind)},
                                    {QStringLiteral("title"), p.title},
                                    {QStringLiteral("detail"), p.detail},
                                    {QStringLiteral("fixes"), fixes}});
    }
    return {{QStringLiteral("resolved"), plan.resolved()}, {QStringLiteral("summary"), plan.summary},
            {QStringLiteral("total"), plan.total}, {QStringLiteral("new"), plan.newPackages + plan.newSlots},
            {QStringLiteral("upgrades"), plan.upgrades}, {QStringLiteral("downgrades"), plan.downgrades},
            {QStringLiteral("rebuilds"), plan.reinstalls}, {QStringLiteral("binaries"), plan.binaries},
            {QStringLiteral("downloadKiB"), double(plan.downloadKiB)},
            {QStringLiteral("rebuildCauses"), plan.rebuildCauses},
            {QStringLiteral("merges"), merges}, {QStringLiteral("problems"), problems}};
}

QVariantMap PlanModel::highlightMap(const Core::EmergePlan &plan)
{
    QVariantMap out;
    for (const Core::MergeItem &it : plan.items) {
        switch (it.action) {
        case Core::MergeItem::New: case Core::MergeItem::NewSlot: out.insert(it.key, QStringLiteral("new")); break;
        case Core::MergeItem::Upgrade: out.insert(it.key, QStringLiteral("upgrade")); break;
        case Core::MergeItem::Downgrade: out.insert(it.key, QStringLiteral("downgrade")); break;
        case Core::MergeItem::Reinstall: case Core::MergeItem::Rebuild: out.insert(it.key, QStringLiteral("rebuild")); break;
        case Core::MergeItem::Uninstall: out.insert(it.key, QStringLiteral("remove")); break;
        default: break;
        }
    }
    return out;
}

} // namespace QindaPortage::App
