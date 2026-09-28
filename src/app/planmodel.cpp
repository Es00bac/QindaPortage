// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/planmodel.h"

#include "app/systemmodel.h"
#include "core/atom.h"
#include "core/version.h"

#include <QDir>
#include <QJsonArray>
#include <QSettings>

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
    m_autonomous = QSettings().value(QStringLiteral("assistant/autonomous"), true).toBool();
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
            m_lastResult = m_runner.result();
            m_preview = previewMap(m_runner.result());
            m_highlights = highlightMap(m_runner.result());
        }
        m_previewRevision = m_runningRevision;
        Q_EMIT previewStateChanged();
        Q_EMIT previewChanged();
        if (m_applyAfterPreview) {
            m_applyAfterPreview = false;
            if (!ok) {
                finishPendingApply(false, {}, QStringLiteral("the preview failed: %1").arg(error), {});
            } else {
                applyIfAllowed();
            }
        }
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
                finishPendingApply(ok, txn, error, tail);
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
    m_runningRevision = m_revision;
    if (targets.isEmpty()) {
        // Config-only plans: what the change does to the installed packages
        // it names (a USE change on an installed package means a rebuild).
        // AGENT-NOTE: not "@world -uDN" — that previews every pending update
        // and would trip the huge-merge risk rule for a one-line change.
        QStringList installed;
        const SystemSnapshot *snap = m_system ? m_system->snapshot() : nullptr;
        for (const PlanStep &s : m_plan.steps) {
            if (s.atom.isEmpty()) continue;
            QString key = s.atom;
            while (!key.isEmpty() && QStringLiteral("=<>~").contains(key.front())) key.remove(0, 1);
            const Core::Atom a = Core::Atom::parse(s.atom);
            if (a.isValid()) key = a.key();
            if (snap && snap->isInstalled(key) && !installed.contains(key)) installed.append(key);
        }
        if (installed.isEmpty()) {
            // Nothing installed is affected: the change only matters when
            // those packages are installed later. Nothing to resolve.
            m_lastResult = Core::EmergePlan();
            m_preview = previewMap(m_lastResult);
            m_highlights.clear();
            m_previewRevision = m_runningRevision;
            Q_EMIT previewChanged();
            if (m_applyAfterPreview) {
                m_applyAfterPreview = false;
                applyIfAllowed();
            }
            return;
        }
        targets = installed;
        options = {QStringLiteral("--oneshot"), QStringLiteral("--newuse"), QStringLiteral("--update")};
    }
    if (!m_runner.start(targets, options, m_plan)) {
        m_preview = {{QStringLiteral("error"), m_runner.error()}};
        Q_EMIT previewChanged();
        if (m_applyAfterPreview) {
            m_applyAfterPreview = false;
            finishPendingApply(false, {}, QStringLiteral("the preview could not start: %1").arg(m_runner.error()), {});
        }
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

void PlanModel::setAutonomous(bool on)
{
    if (on == m_autonomous) return;
    m_autonomous = on;
    QSettings().setValue(QStringLiteral("assistant/autonomous"), on);
    Q_EMIT autonomyChanged();
}

void PlanModel::finishPendingApply(bool ok, const QString &txn, const QString &error, const QString &tail)
{
    if (!m_pendingApply) return;
    ApplyDone done = std::move(m_pendingApply);
    m_pendingApply = nullptr;
    done(ok, txn, error, tail);
}

void PlanModel::applyVerified(const ApplyDone &done, const std::function<QString()> &gate)
{
    if (m_pendingApply || m_executor.isRunning()) {
        done(false, {}, QStringLiteral("a transaction is already running"), {});
        return;
    }
    if (m_plan.isEmpty()) {
        done(false, {}, QStringLiteral("the plan is empty"), {});
        return;
    }
    m_pendingApply = done;
    m_applyGate = gate;
    if (previewCurrent() && !m_runner.isRunning()) {
        applyIfAllowed();
        return;
    }
    m_applyAfterPreview = true;
    runPreview();
}

void PlanModel::applyIfAllowed()
{
    if (!previewCurrent() || !m_lastResult.resolved()) {
        QStringList problems;
        for (const Core::Problem &p : m_lastResult.problems) problems.append(p.title);
        finishPendingApply(false, {}, QStringLiteral("not applied: the preview does not resolve (%1)")
                                          .arg(problems.join(QStringLiteral("; "))), {});
        return;
    }
    const QString refused = m_applyGate ? m_applyGate() : QString();
    m_applyGate = nullptr;
    if (!refused.isEmpty()) {
        finishPendingApply(false, {}, refused, {});
        return;
    }
    if (!apply()) {
        finishPendingApply(false, {}, QStringLiteral("could not start the helper: %1").arg(m_log.join(QLatin1Char('\n'))), {});
    }
}

QStringList PlanModel::riskReasons() const
{
    // AGENT-NOTE: the assistant's autonomy ends here; each rule names a
    // change that can leave the system unbootable or unbuildable, or that
    // overrides a Gentoo developer's decision.
    static const QStringList critical = {
        QStringLiteral("sys-libs/glibc"), QStringLiteral("sys-devel/gcc"), QStringLiteral("sys-devel/binutils"),
        QStringLiteral("dev-lang/python"), QStringLiteral("sys-apps/portage"), QStringLiteral("sys-apps/systemd"),
        QStringLiteral("dev-lang/perl"), QStringLiteral("llvm-core/llvm"), QStringLiteral("llvm-core/clang"),
        QStringLiteral("sys-libs/ncurses"), QStringLiteral("sys-apps/coreutils"), QStringLiteral("app-shells/bash")};
    auto keyOf = [](QString atom) {
        while (!atom.isEmpty() && QStringLiteral("=<>~!").contains(atom.front())) atom.remove(0, 1);
        atom = atom.section(QLatin1Char(':'), 0, 0).section(QStringLiteral("::"), 0, 0);
        QString pn, pv;
        const QString cat = atom.section(QLatin1Char('/'), 0, 0);
        const QString rest = atom.section(QLatin1Char('/'), 1);
        return Core::Version::splitNameVersion(rest, &pn, &pv) ? cat + QLatin1Char('/') + pn : atom;
    };
    auto isSystem = [this](const QString &key) {
        const SystemSnapshot *s = m_system ? m_system->snapshot() : nullptr;
        if (!s) return false;
        for (int idx : s->db.byKey(key)) if (s->graph.isSystem(idx)) return true;
        return false;
    };
    QStringList out;
    for (const PlanStep &step : m_plan.steps) {
        if (step.kind == PlanStep::Unmask) out.append(QStringLiteral("unmasks %1, which Gentoo masked on purpose").arg(step.atom));
        if (step.kind == PlanStep::AcceptKeyword && step.values.contains(QStringLiteral("**"))) {
            out.append(QStringLiteral("accepts unkeyworded/live versions of %1").arg(step.atom));
        }
        if (step.kind == PlanStep::Depclean && step.values.isEmpty()) out.append(QStringLiteral("removes every orphan at once"));
        if (step.kind == PlanStep::Unmerge || step.kind == PlanStep::Depclean) {
            for (const QString &v : step.values) {
                const QString key = keyOf(v);
                if (critical.contains(key) || isSystem(key)) out.append(QStringLiteral("removes %1, part of the base system").arg(key));
            }
        }
    }
    if (previewCurrent()) {
        int merges = 0;
        for (const Core::MergeItem &it : m_lastResult.items) {
            if (it.action == Core::MergeItem::NoMerge || it.action == Core::MergeItem::Block) continue;
            ++merges;
            if (it.action == Core::MergeItem::Downgrade) out.append(QStringLiteral("downgrades %1 to %2").arg(it.key, it.version));
            if (it.action == Core::MergeItem::Uninstall && (critical.contains(it.key) || isSystem(it.key))) {
                out.append(QStringLiteral("uninstalls %1, part of the base system").arg(it.key));
            }
        }
        if (merges > 80) out.append(QStringLiteral("merges %1 packages").arg(merges));
    }
    out.removeDuplicates();
    return out;
}

} // namespace QindaPortage::App
