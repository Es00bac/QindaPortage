// SPDX-License-Identifier: GPL-3.0-or-later
//
// The assistant's tools that act on the system (docs/design.md "Safety
// rules"). Everything still goes through PlanModel -> Executor -> the root
// helper, which re-validates every step, writes only managed files,
// snapshots first and journals. What the assistant may do on its own is
// decided here in code, not by the model:
//   - acting on the user's behalf must be switched on (PlanModel::autonomous);
//   - the plan is previewed again and must resolve;
//   - PlanModel::riskReasons() (system package removal, toolchain
//     downgrades, unmasking, live ebuilds, huge merges) always needs the user.

#include "app/planmodel.h"
#include "app/toolbox.h"

#include <QJsonArray>
#include <QProcess>

namespace QindaPortage::App {

using namespace Agent;

namespace {

QJsonObject error(const QString &message)
{
    return QJsonObject{{QStringLiteral("error"), message}};
}

QString runQuick(const QString &program, const QStringList &args, int timeoutMs = 15000)
{
    QProcess p;
    p.start(program, args);
    if (!p.waitForFinished(timeoutMs)) {
        p.kill();
        return {};
    }
    return QString::fromUtf8(p.readAllStandardOutput());
}

} // namespace

void Toolbox::registerActionTools()
{
    using R = ToolRegistry;

    m_registry.add({QStringLiteral("apply_plan"),
                    QStringLiteral("Carry out the draft plan now: it is previewed again, must resolve, and is applied as root by the "
                                   "QindaPortage helper (configuration snapshot first, transaction journaled). Blocks until the "
                                   "build finishes and returns the result; the model is unloaded while it builds. Some changes "
                                   "always need the user's approval; the answer says so."),
                    R::object({{QStringLiteral("summary"), R::string(QStringLiteral("one line: what this transaction does"))}},
                              {QStringLiteral("summary")}),
                    [this](const QJsonObject &a, const ToolDone &done) {
                        if (!m_plan) { done(error(QStringLiteral("no plan available"))); return; }
                        if (m_plan->stepCount() == 0) { done(error(QStringLiteral("the plan is empty; add steps with plan_* first"))); return; }
                        if (!m_plan->autonomous()) {
                            done({{QStringLiteral("applied"), false},
                                  {QStringLiteral("needsUser"), QStringLiteral("acting on the user's behalf is switched off")},
                                  {QStringLiteral("next"), QStringLiteral("summarise the plan and tell the user to say \"apply\" or press Apply")}});
                            return;
                        }
                        const QString summary = a.value(QStringLiteral("summary")).toString().trimmed();
                        if (!summary.isEmpty() && m_plan->goal().isEmpty()) m_plan->setGoal(summary);
                        m_agentApplying = true;
                        m_plan->applyVerified(
                            [this, done](bool ok, const QString &txn, const QString &err, const QString &tail) {
                                m_agentApplying = false;
                                if (err.startsWith(QLatin1String("needs the user:"))) {
                                    done({{QStringLiteral("applied"), false}, {QStringLiteral("needsUser"), err.mid(15).trimmed()},
                                          {QStringLiteral("next"), QStringLiteral("explain these risks and tell the user to say \"apply\" or press Apply if they agree")}});
                                    return;
                                }
                                QJsonObject o{{QStringLiteral("applied"), ok}, {QStringLiteral("transaction"), txn}};
                                if (!err.isEmpty()) o[QStringLiteral("error")] = err;
                                if (!ok && !tail.isEmpty()) o[QStringLiteral("logTail")] = tail.right(3000);
                                o[QStringLiteral("next")] = ok ? QStringLiteral("report what changed; revert_transaction can undo the configuration part")
                                                               : QStringLiteral("find the cause (build_log for the failed package), fix the plan, pretend, apply again");
                                done(o);
                            },
                            [this]() -> QString {
                                const QStringList risks = m_plan->riskReasons();
                                return risks.isEmpty() ? QString()
                                                       : QStringLiteral("needs the user: ") + risks.join(QStringLiteral("; "));
                            });
                    },
                    true});

    m_registry.add({QStringLiteral("revert_transaction"),
                    QStringLiteral("Undo the configuration a transaction wrote (package.use/keywords/licenses, patches, ebuilds in "
                                   "qinda-local), when nobody edited those files since. Packages are not rebuilt; the answer lists "
                                   "the emerge commands that would, which you can put in a plan."),
                    R::object({{QStringLiteral("transaction"), R::string(QStringLiteral("id from list_transactions"))}},
                              {QStringLiteral("transaction")}),
                    [this](const QJsonObject &a, const ToolDone &done) {
                        if (!m_plan) { done(error(QStringLiteral("no plan available"))); return; }
                        if (!m_plan->autonomous()) { done(error(QStringLiteral("acting on the user's behalf is switched off; ask the user to revert it from Activity"))); return; }
                        const QString txn = a.value(QStringLiteral("transaction")).toString().trimmed();
                        auto *conn = new QMetaObject::Connection;
                        *conn = connect(m_plan->executor(), &Ops::Executor::finished, this,
                                        [this, done, conn](bool ok, const QString &id, const QString &err) {
                                            disconnect(*conn);
                                            delete conn;
                                            m_plan->refreshJournal();
                                            done({{QStringLiteral("reverted"), ok}, {QStringLiteral("transaction"), id},
                                                  {QStringLiteral("error"), err},
                                                  {QStringLiteral("undoCommands"), QJsonArray::fromStringList(m_plan->undoCommands())}});
                                        });
                        if (!m_plan->revert(txn)) {
                            disconnect(*conn);
                            delete conn;
                            done(error(QStringLiteral("could not start the revert (another transaction running?)")));
                        }
                    },
                    true});

    m_registry.add({QStringLiteral("list_transactions"),
                    QStringLiteral("Recent transactions the helper applied: id, what, result, packages merged/removed, reverted or not."),
                    R::object({}, {}),
                    [this](const QJsonObject &, const ToolDone &done) {
                        if (!m_plan) { done(error(QStringLiteral("no plan available"))); return; }
                        m_plan->refreshJournal();
                        QJsonArray list;
                        for (const QVariant &v : m_plan->journal()) {
                            const QVariantMap e = v.toMap();
                            list.append(QJsonObject{{QStringLiteral("id"), e.value(QStringLiteral("id")).toString()},
                                                    {QStringLiteral("kind"), e.value(QStringLiteral("kind")).toString()},
                                                    {QStringLiteral("title"), e.value(QStringLiteral("title")).toString()},
                                                    {QStringLiteral("ok"), e.value(QStringLiteral("ok")).toBool()},
                                                    {QStringLiteral("merged"), QJsonArray::fromStringList(e.value(QStringLiteral("merged")).toStringList())},
                                                    {QStringLiteral("removed"), QJsonArray::fromStringList(e.value(QStringLiteral("removed")).toStringList())},
                                                    {QStringLiteral("revertedBy"), e.value(QStringLiteral("reverted_by")).toString()}});
                            if (list.size() >= 15) break;
                        }
                        done({{QStringLiteral("transactions"), list}});
                    }});

    m_registry.add({QStringLiteral("read_news"),
                    QStringLiteral("Gentoo news items (eselect news): list them, or read one by number. Unread items can require "
                                   "manual steps before an update."),
                    R::object({{QStringLiteral("item"), R::integer(QStringLiteral("number to read; omit to list"))}}, {}),
                    [](const QJsonObject &a, const ToolDone &done) {
                        const int item = a.value(QStringLiteral("item")).toInt(0);
                        if (item <= 0) {
                            const QString list = runQuick(QStringLiteral("eselect"), {QStringLiteral("news"), QStringLiteral("list")});
                            // The unread ones are marked "N" by eselect; keep the tail (newest).
                            QStringList lines = list.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
                            done({{QStringLiteral("items"), lines.mid(std::max<qsizetype>(0, lines.size() - 25)).join(QLatin1Char('\n'))}});
                            return;
                        }
                        const QString text = runQuick(QStringLiteral("eselect"),
                                                      {QStringLiteral("news"), QStringLiteral("read"), QStringLiteral("--raw"),
                                                       QString::number(item)});
                        done(text.isEmpty() ? error(QStringLiteral("no such news item"))
                                            : QJsonObject{{QStringLiteral("text"), text.left(6000)}});
                    }});
}

} // namespace QindaPortage::App
