// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/sourcesmodel.h"

#include "app/planmodel.h"
#include "app/systemmodel.h"
#include "ops/ebuilddraft.h"
#include "ops/plan.h"

#include <QJsonArray>

namespace QindaPortage::App {

SourcesModel::SourcesModel(SystemModel *system, PlanModel *plan, QObject *parent)
    : QObject(parent)
    , m_system(system)
    , m_plan(plan)
{
    connect(&m_probe, &Ops::SourceProbe::busyChanged, this, &SourcesModel::stateChanged);
    connect(&m_probe, &Ops::SourceProbe::progress, this, [this](const QString &message) {
        m_progress = message;
        Q_EMIT stateChanged();
    });
    connect(&m_probe, &Ops::SourceProbe::finished, this, [this](const Ops::SourceInfo &info) {
        m_info = info;
        m_progress.clear();
        const Core::RepoIndex *index = m_system->snapshot() ? &m_system->snapshot()->index : nullptr;
        m_resolutions = info.ok() ? Ops::DependencyHints::resolve(info.dependencies, index)
                                  : QVector<Ops::DependencyResolution>{};
        m_category = info.ok() ? Ops::EbuildDraft::suggestedCategory(info) : QString();
        m_draft = info.ok() ? Ops::EbuildDraft::generate(info, Ops::DependencyHints::atomMap(m_resolutions)) : QString();
        Q_EMIT stateChanged();
        Q_EMIT resultChanged();
        Q_EMIT probed(info.ok());
    });
}

QVariantMap SourcesModel::info() const
{
    return m_info.toJson(false).toVariantMap();
}

QVariantList SourcesModel::dependencies() const
{
    return Ops::DependencyHints::toJson(m_resolutions).toVariantList();
}

void SourcesModel::setCategory(const QString &c)
{
    if (c == m_category) return;
    m_category = c.trimmed();
    Q_EMIT resultChanged();
}

QString SourcesModel::packageName() const
{
    return m_info.ok() && !m_info.url.isEmpty() ? Ops::EbuildDraft::packageName(m_info) : QString();
}

QString SourcesModel::ebuildPath() const
{
    if (packageName().isEmpty() || m_category.isEmpty()) return {};
    return m_category + QLatin1Char('/') + packageName() + QLatin1Char('/') + Ops::EbuildDraft::fileName(m_info);
}

QStringList SourcesModel::alreadyPackaged() const
{
    const SystemSnapshot *snap = m_system->snapshot();
    const QString pn = packageName();
    if (!snap || pn.isEmpty()) return {};
    QStringList out;
    for (const Core::SearchHit &h : snap->index.search(pn, 10)) {
        if (h.key.section(QLatin1Char('/'), 1).compare(pn, Qt::CaseInsensitive) != 0) continue;
        const Core::AvailablePackage *p = snap->index.find(h.key);
        const QString repo = p && !p->versions.isEmpty() ? p->versions.last().repo : QString();
        if (repo == QLatin1String("qinda-local")) continue;
        out.append(h.key + QLatin1Char('-') + h.bestVersion + (repo.isEmpty() ? QString() : QStringLiteral("::") + repo));
    }
    return out;
}

void SourcesModel::setDraft(const QString &text)
{
    if (text == m_draft) return;
    m_draft = text;
    Q_EMIT resultChanged();
}

void SourcesModel::probe(const QString &url)
{
    m_progress = tr("starting…");
    Q_EMIT stateChanged();
    m_probe.probe(url.trimmed());
}

void SourcesModel::cancel()
{
    m_probe.cancel();
}

QString SourcesModel::addToPlan()
{
    const QString path = ebuildPath();
    if (path.isEmpty() || m_draft.trimmed().isEmpty()) return tr("probe a source first");
    const QString rationale = tr("package %1 from %2 so Portage tracks it").arg(packageName(), m_info.url);
    Ops::PlanStep ebuild;
    ebuild.kind = Ops::PlanStep::WriteEbuild;
    ebuild.path = path;
    ebuild.content = m_draft;
    ebuild.rationale = rationale;
    if (const QString why = m_plan->addStep(ebuild); !why.isEmpty()) return why;
    // AGENT-NOTE: drafts carry KEYWORDS="~amd64" (honest for an unreviewed
    // package), so a stable system needs this exact version accepted.
    Ops::PlanStep keyword;
    keyword.kind = Ops::PlanStep::AcceptKeyword;
    keyword.atom = QStringLiteral("=%1/%2-%3").arg(m_category, packageName(), m_info.version);
    keyword.values = {QStringLiteral("~amd64")};
    keyword.rationale = tr("new local package %1; accept its testing keyword for this version only").arg(packageName());
    if (const QString why = m_plan->addStep(keyword); !why.isEmpty()) return why;
    Ops::PlanStep install;
    install.kind = Ops::PlanStep::Emerge;
    install.values = {QStringLiteral("=%1/%2-%3::qinda-local").arg(m_category, packageName(), m_info.version)};
    install.rationale = tr("install %1 from the qinda-local overlay").arg(packageName());
    if (m_plan->goal().isEmpty()) m_plan->setGoal(tr("Install %1").arg(m_info.url));
    return m_plan->addStep(install);
}

} // namespace QindaPortage::App
