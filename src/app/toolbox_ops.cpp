// SPDX-License-Identifier: GPL-3.0-or-later
//
// The assistant tools that reach the ops layer: virtual pretend, the plan_*
// builders, build logs, and the web (Gentoo bugzilla, upstream pages).

#include "app/planmodel.h"
#include "app/toolbox.h"
#include "core/buildlog.h"
#include "ops/plan.h"
#include "ops/pretendrunner.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QRegularExpression>
#include <QTextDocumentFragment>
#include <QUrlQuery>

namespace QindaPortage::App {

using namespace Agent;
using Ops::PlanStep;

namespace {

QJsonObject error(const QString &message)
{
    return QJsonObject{{QStringLiteral("error"), message}};
}

QStringList strings(const QJsonValue &v)
{
    QStringList out;
    if (v.isString()) out = v.toString().split(QRegularExpression(QStringLiteral("[\\s,]+")), Qt::SkipEmptyParts);
    for (const QJsonValue &x : v.toArray()) out.append(x.toString().trimmed());
    out.removeAll(QString());
    return out;
}

// Pretend arguments {"use": {"cat/pn": ["+x"]}, ...} -> plan steps.
void addMapSteps(const QJsonValue &map, PlanStep::Kind kind, Ops::Plan *plan)
{
    const QJsonObject o = map.toObject();
    for (auto it = o.begin(); it != o.end(); ++it) {
        PlanStep s;
        s.kind = kind;
        s.atom = it.key();
        s.values = strings(it.value());
        s.rationale = QStringLiteral("trial");
        plan->steps.append(s);
    }
}

// HTML page -> readable text, bounded.
QString pageText(const QByteArray &body, const QString &contentType, int limit)
{
    QString text;
    if (contentType.contains(QLatin1String("html"))) {
        QString html = QString::fromUtf8(body);
        html.remove(QRegularExpression(QStringLiteral("<(script|style)[^>]*>.*?</\\1>"),
                                       QRegularExpression::DotMatchesEverythingOption | QRegularExpression::CaseInsensitiveOption));
        text = QTextDocumentFragment::fromHtml(html).toPlainText();
    } else {
        text = QString::fromUtf8(body);
    }
    text.replace(QRegularExpression(QStringLiteral("\\n{3,}")), QStringLiteral("\n\n"));
    return text.left(limit);
}

} // namespace

void Toolbox::webGet(const QUrl &url, const std::function<void(QByteArray, QString)> &done)
{
    if (!m_net) m_net = new QNetworkAccessManager(this);
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("QindaPortage/0.1 (Gentoo package assistant)"));
    request.setTransferTimeout(20000);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    QNetworkReply *reply = m_net->get(request);
    connect(reply, &QNetworkReply::finished, this, [reply, done] {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            done({}, reply->errorString());
            return;
        }
        done(reply->read(2 * 1024 * 1024), reply->header(QNetworkRequest::ContentTypeHeader).toString());
    });
}

void Toolbox::runPretend(const QJsonObject &args, const ToolDone &done)
{
    QStringList atoms = strings(args.value(QStringLiteral("atoms")));
    QStringList options = strings(args.value(QStringLiteral("options")));
    for (const QString &a : atoms) {
        QString why;
        if (!a.startsWith(QLatin1Char('@')) && !Ops::Validate::isAtom(a, &why)) {
            QJsonObject e = error(QStringLiteral("invalid atom %1: %2").arg(a, why));
            const QStringList guesses = suggestKeys(a);
            if (!guesses.isEmpty()) e[QStringLiteral("didYouMean")] = QJsonArray::fromStringList(guesses);
            done(e);
            return;
        }
    }
    for (const QString &o : options) {
        if (!Ops::Validate::isAllowedEmergeOption(o)) {
            done(error(QStringLiteral("option %1 is not allowed").arg(o)));
            return;
        }
    }
    // The trial configuration: the draft plan's config, patches and ebuilds
    // (so pretend sees what has been decided so far), plus this call's
    // extra changes.
    Ops::Plan trial;
    if (m_plan && args.value(QStringLiteral("withPlan")).toBool(true)) {
        for (const PlanStep &s : m_plan->plan().steps) {
            if (!s.isBuild()) trial.steps.append(s);
            else if (atoms.isEmpty() && s.kind == PlanStep::Emerge) atoms += s.values;
        }
    }
    addMapSteps(args.value(QStringLiteral("use")), PlanStep::SetUse, &trial);
    addMapSteps(args.value(QStringLiteral("keywords")), PlanStep::AcceptKeyword, &trial);
    addMapSteps(args.value(QStringLiteral("licenses")), PlanStep::AcceptLicense, &trial);
    for (const PlanStep &s : trial.steps) {
        const QString why = s.validate();
        if (!why.isEmpty()) {
            done(error(QStringLiteral("trial change rejected: %1").arg(why)));
            return;
        }
    }
    if (atoms.isEmpty()) {
        done(error(QStringLiteral("give atoms to resolve (or @world with options --update --deep --newuse)")));
        return;
    }
    if (!m_pretend) m_pretend = new Ops::PretendRunner(this);
    if (m_pretend->isRunning()) m_pretend->cancel();
    m_pretend->disconnect(this);
    connect(m_pretend, &Ops::PretendRunner::finished, this, [this, done](bool ok, const QString &err) {
        m_pretend->disconnect(this);
        if (!ok) {
            done(error(err.isEmpty() ? QStringLiteral("emerge --pretend failed") : err));
            return;
        }
        QJsonObject result = m_pretend->result().toJson(40);
        if (m_pretend->result().items.isEmpty() && m_pretend->result().problems.isEmpty()) {
            // Unknown output shape: give the model the raw tail.
            result[QStringLiteral("raw")] = m_pretend->rawOutput().right(2500);
        }
        done(result);
    });
    if (!m_pretend->start(atoms, options, trial)) {
        m_pretend->disconnect(this);
        done(error(m_pretend->error()));
    }
}

QJsonObject Toolbox::planStep(const QJsonObject &json)
{
    if (!m_plan) return error(QStringLiteral("no plan available"));
    bool ok = false;
    const PlanStep step = PlanStep::fromJson(json, &ok);
    if (!ok) return error(QStringLiteral("bad step"));
    if (step.rationale.trimmed().isEmpty()) return error(QStringLiteral("give a one-line rationale"));
    const QString why = m_plan->addStep(step);
    if (!why.isEmpty()) {
        QJsonObject e = error(why);
        if (!step.atom.isEmpty()) {
            const QStringList guesses = suggestKeys(step.atom);
            if (!guesses.isEmpty()) e[QStringLiteral("didYouMean")] = QJsonArray::fromStringList(guesses);
        }
        return e;
    }
    return QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("added"), step.describe()},
                       {QStringLiteral("plan"), QJsonArray::fromStringList(m_plan->plan().describe())},
                       {QStringLiteral("next"), QStringLiteral("run pretend to verify the plan resolves")}};
}

void Toolbox::registerOpsTools(PlanModel *plan)
{
    m_plan = plan;
    using R = ToolRegistry;
    const QJsonObject flagMap{{QStringLiteral("type"), QStringLiteral("object")},
                              {QStringLiteral("description"), QStringLiteral("atom -> list of values, e.g. {\"app-misc/jq\": [\"+static-libs\"]}")}};
    m_registry.add({QStringLiteral("pretend"),
                    QStringLiteral("Resolve atoms with emerge --pretend on a virtual copy of the configuration that already includes the draft plan's config changes, plus optional extra trial USE/keyword/license changes. Returns merges, problems and candidate fixes."),
                    R::object({{QStringLiteral("atoms"), R::stringArray(QStringLiteral("atoms or @sets; empty = the plan's emerge targets"))},
                               {QStringLiteral("use"), flagMap}, {QStringLiteral("keywords"), flagMap},
                               {QStringLiteral("licenses"), flagMap},
                               {QStringLiteral("options"), R::stringArray(QStringLiteral("e.g. --update --deep --newuse --oneshot"))}},
                              {}),
                    [this](const QJsonObject &a, const ToolDone &done) { runPretend(a, done); }});

    auto planTool = [this](const char *name, const char *description, PlanStep::Kind kind, QJsonObject props,
                           QStringList required) {
        m_registry.add({QString::fromLatin1(name), QString::fromUtf8(description), R::object(props, required),
                        [this, kind](const QJsonObject &a, const ToolDone &done) {
                            QJsonObject step{{QStringLiteral("kind"), PlanStep::kindName(kind)},
                                             {QStringLiteral("atom"), a.value(QStringLiteral("atom"))},
                                             {QStringLiteral("rationale"), a.value(QStringLiteral("rationale"))},
                                             {QStringLiteral("path"), a.value(QStringLiteral("path")).toString(
                                                                          a.value(QStringLiteral("filename")).toString())},
                                             {QStringLiteral("content"), a.value(QStringLiteral("content")).toString(
                                                                             a.value(QStringLiteral("diff")).toString())},
                                             {QStringLiteral("options"), QJsonArray::fromStringList(strings(a.value(QStringLiteral("options"))))}};
                            QStringList values = strings(a.value(QStringLiteral("flags")));
                            values += strings(a.value(QStringLiteral("keywords")));
                            values += strings(a.value(QStringLiteral("licenses")));
                            values += strings(a.value(QStringLiteral("atoms")));
                            values += strings(a.value(QStringLiteral("repos")));
                            if (kind == PlanStep::AcceptKeyword && values.isEmpty()) values = {QStringLiteral("~amd64")};
                            step[QStringLiteral("values")] = QJsonArray::fromStringList(values);
                            done(planStep(step));
                        },
                        true});
    };
    const QJsonObject atom{{QStringLiteral("atom"), R::string(QStringLiteral("category/name, or =category/name-version"))}};
    const QJsonObject why{{QStringLiteral("rationale"), R::string(QStringLiteral("one line a human would accept"))}};
    auto merge = [](QJsonObject a, const QJsonObject &b) { for (auto it = b.begin(); it != b.end(); ++it) a.insert(it.key(), it.value()); return a; };

    planTool("plan_use", "Add a package.use change to the draft plan.", PlanStep::SetUse,
             merge(merge(atom, why), {{QStringLiteral("flags"), R::stringArray(QStringLiteral("\"+flag\" enables, \"-flag\" disables"))}}),
             {QStringLiteral("atom"), QStringLiteral("flags"), QStringLiteral("rationale")});
    planTool("plan_keyword", "Accept a testing keyword for one package (prefer =cat/pn-version).", PlanStep::AcceptKeyword,
             merge(merge(atom, why), {{QStringLiteral("keywords"), R::stringArray(QStringLiteral("default [\"~amd64\"]; \"**\" for live ebuilds"))}}),
             {QStringLiteral("atom"), QStringLiteral("rationale")});
    planTool("plan_license", "Accept a license for one package.", PlanStep::AcceptLicense,
             merge(merge(atom, why), {{QStringLiteral("licenses"), R::stringArray(QStringLiteral("license names"))}}),
             {QStringLiteral("atom"), QStringLiteral("licenses"), QStringLiteral("rationale")});
    planTool("plan_unmask", "Unmask a package.mask'ed version.", PlanStep::Unmask, merge(atom, why),
             {QStringLiteral("atom"), QStringLiteral("rationale")});
    planTool("plan_patch", "Add a user patch applied by the ebuild's default src_prepare (/etc/portage/patches/<cat>/<pn>/).",
             PlanStep::AddPatch,
             merge(merge(atom, why), {{QStringLiteral("filename"), R::string(QStringLiteral("e.g. fix-gcc15.patch"))},
                                      {QStringLiteral("diff"), R::string(QStringLiteral("unified diff, paths relative to the source tree (-p1)"))}}),
             {QStringLiteral("atom"), QStringLiteral("filename"), QStringLiteral("diff"), QStringLiteral("rationale")});
    planTool("plan_ebuild", "Write a file into the qinda-local overlay: cat/pn/pn-ver.ebuild, cat/pn/metadata.xml or cat/pn/files/<name>.",
             PlanStep::WriteEbuild,
             merge(why, {{QStringLiteral("path"), R::string(QStringLiteral("cat/pn/pn-ver.ebuild"))},
                         {QStringLiteral("content"), R::string(QStringLiteral("the complete file"))}}),
             {QStringLiteral("path"), QStringLiteral("content"), QStringLiteral("rationale")});
    planTool("plan_emerge", "Install or update packages (emerge).", PlanStep::Emerge,
             merge(why, {{QStringLiteral("atoms"), R::stringArray(QStringLiteral("atoms or @sets"))},
                         {QStringLiteral("options"), R::stringArray(QStringLiteral("e.g. --oneshot, --update --deep --newuse"))}}),
             {QStringLiteral("atoms"), QStringLiteral("rationale")});
    planTool("plan_remove", "Remove packages safely (deselect then depclean; refuses if something still needs them).",
             PlanStep::Unmerge, merge(why, {{QStringLiteral("atoms"), R::stringArray(QStringLiteral("category/name"))}}),
             {QStringLiteral("atoms"), QStringLiteral("rationale")});
    planTool("plan_depclean", "Remove orphans (all, or only the given ones).", PlanStep::Depclean,
             merge(why, {{QStringLiteral("atoms"), R::stringArray(QStringLiteral("optional"))}}), {QStringLiteral("rationale")});
    planTool("plan_sync", "Sync repositories before other steps.", PlanStep::Sync,
             merge(why, {{QStringLiteral("repos"), R::stringArray(QStringLiteral("empty = all"))}}), {QStringLiteral("rationale")});

    m_registry.add({QStringLiteral("plan_show"), QStringLiteral("The draft plan's steps (index: description)."),
                    R::object({}, {}),
                    [this](const QJsonObject &, const ToolDone &done) {
                        done({{QStringLiteral("plan"), QJsonArray::fromStringList(m_plan->plan().describe())}});
                    }});
    m_registry.add({QStringLiteral("plan_drop"), QStringLiteral("Remove step `index` (0-based) from the draft plan."),
                    R::object({{QStringLiteral("index"), R::integer(QStringLiteral("0-based"))}}, {QStringLiteral("index")}),
                    [this](const QJsonObject &a, const ToolDone &done) {
                        m_plan->removeStep(a.value(QStringLiteral("index")).toInt(-1));
                        done({{QStringLiteral("plan"), QJsonArray::fromStringList(m_plan->plan().describe())}});
                    },
                    true});

    m_registry.add({QStringLiteral("build_log"),
                    QStringLiteral("The last build log of a package, classified: failed phase, kind (missing-header, missing-library, cmake-config, compiler-error, patch-failed, out-of-memory, …), missing names, first errors and the tail."),
                    R::object({{QStringLiteral("atom"), R::string(QStringLiteral("category/name"))}}, {QStringLiteral("atom")}),
                    [](const QJsonObject &a, const ToolDone &done) {
                        const QString path = Core::BuildLog::find(a.value(QStringLiteral("atom")).toString());
                        if (path.isEmpty()) {
                            done(error(QStringLiteral("no build log under /var/tmp/portage for that package")));
                            return;
                        }
                        done(Core::BuildLog::analyseFile(path).toJson());
                    }});
    m_registry.add({QStringLiteral("web_fetch"),
                    QStringLiteral("Fetch a web page as text (bug reports, upstream issues, patches, release notes). http(s) only."),
                    R::object({{QStringLiteral("url"), R::string(QStringLiteral("https://…"))}}, {QStringLiteral("url")}),
                    [this](const QJsonObject &a, const ToolDone &done) {
                        const QUrl url(a.value(QStringLiteral("url")).toString());
                        if (!url.isValid() || (url.scheme() != QLatin1String("https") && url.scheme() != QLatin1String("http"))) {
                            done(error(QStringLiteral("only http(s) URLs")));
                            return;
                        }
                        webGet(url, [done, url](const QByteArray &body, const QString &typeOrError) {
                            if (body.isEmpty()) { done(error(typeOrError)); return; }
                            done({{QStringLiteral("url"), url.toString()}, {QStringLiteral("text"), pageText(body, typeOrError, 9000)}});
                        });
                    }});
    m_registry.add({QStringLiteral("search_bugs"),
                    QStringLiteral("Search Gentoo Bugzilla (package name plus error words). Returns bug ids, titles and status; read one with web_fetch https://bugs.gentoo.org/<id>."),
                    R::object({{QStringLiteral("query"), R::string(QStringLiteral("e.g. \"media-video/mpv gcc 15\""))}}, {QStringLiteral("query")}),
                    [this](const QJsonObject &a, const ToolDone &done) {
                        QUrl url(QStringLiteral("https://bugs.gentoo.org/rest/bug"));
                        QUrlQuery q;
                        q.addQueryItem(QStringLiteral("quicksearch"), a.value(QStringLiteral("query")).toString());
                        q.addQueryItem(QStringLiteral("limit"), QStringLiteral("10"));
                        q.addQueryItem(QStringLiteral("include_fields"), QStringLiteral("id,summary,status,resolution,last_change_time"));
                        url.setQuery(q);
                        webGet(url, [done](const QByteArray &body, const QString &typeOrError) {
                            const QJsonObject o = QJsonDocument::fromJson(body).object();
                            if (!o.contains(QStringLiteral("bugs"))) { done(error(typeOrError.isEmpty() ? QStringLiteral("no answer") : typeOrError)); return; }
                            done({{QStringLiteral("bugs"), o.value(QStringLiteral("bugs"))}});
                        });
                    }});
}

} // namespace QindaPortage::App
