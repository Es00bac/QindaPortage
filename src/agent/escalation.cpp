// SPDX-License-Identifier: GPL-3.0-or-later
#include "agent/escalation.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>

namespace QindaPortage::Agent {

namespace {

constexpr int MaxBriefChars = 14000;

const char *const Instructions =
    "You are consulted by a local package-management assistant on a Gentoo Linux machine. "
    "It could not solve the problem below. Answer with a concrete, minimal fix: which "
    "package.use / package.accept_keywords / package.license entries, which patch (as a "
    "unified diff with the file path relative to the package's source tree), or which ebuild "
    "changes (full ebuild text for the qinda-local overlay). Cite the Gentoo bug or upstream "
    "commit when you rely on one. Never suggest pip, npm -g, curl|sh or make install. You "
    "cannot run commands on the machine; the local assistant will verify your answer with "
    "emerge --pretend before the user approves it.";

} // namespace

CloudEscalation::CloudEscalation(QObject *parent)
    : QObject(parent)
{
}

QStringList CloudEscalation::providers() const
{
    QStringList out;
    if (!QStandardPaths::findExecutable(m_claude).isEmpty()) out.append(QStringLiteral("claude"));
    if (!QStandardPaths::findExecutable(m_codex).isEmpty()) out.append(QStringLiteral("codex"));
    return out;
}

void CloudEscalation::setProgram(const QString &provider, const QString &program)
{
    if (provider == QLatin1String("claude")) m_claude = program;
    else m_codex = program;
}

QString CloudEscalation::buildBrief(const QString &goal, const QString &systemProfile,
                                    const QVariantList &transcript, const QString &plan)
{
    QString brief = QString::fromUtf8(Instructions);
    brief += QStringLiteral("\n\n## Goal\n") + goal.trimmed();
    brief += QStringLiteral("\n\n## System\n") + systemProfile.trimmed();
    if (!plan.trimmed().isEmpty()) brief += QStringLiteral("\n\n## Draft plan so far\n") + plan.trimmed();
    // Most recent evidence matters most: walk back from the end.
    QStringList steps;
    int budget = MaxBriefChars - brief.size() - 200;
    for (int i = transcript.size() - 1; i >= 0 && budget > 0; --i) {
        const QVariantMap e = transcript.at(i).toMap();
        const QString kind = e.value(QStringLiteral("kind")).toString();
        QString line;
        if (kind == QLatin1String("tool")) {
            line = QStringLiteral("- tool %1(%2) -> %3").arg(e.value(QStringLiteral("tool")).toString(),
                                                              e.value(QStringLiteral("args")).toString().left(300),
                                                              e.value(QStringLiteral("text")).toString());
        } else if (kind != QLatin1String("user")) {
            line = QStringLiteral("- %1: %2").arg(kind, e.value(QStringLiteral("text")).toString().left(1500));
        }
        if (line.isEmpty()) continue;
        budget -= line.size();
        if (budget < 0) break;
        steps.prepend(line);
    }
    brief += QStringLiteral("\n\n## What was tried (oldest first)\n") + steps.join(QLatin1Char('\n'));
    return brief;
}

QStringList CloudEscalation::arguments(const QString &provider, const QString &brief) const
{
    if (provider == QLatin1String("claude")) {
        // AGENT-GUARD: no Bash/Edit/Write — the consultant must not act on
        // the machine; web research only.
        return {QStringLiteral("-p"), QStringLiteral("--output-format"), QStringLiteral("json"),
                QStringLiteral("--allowedTools"), QStringLiteral("WebSearch,WebFetch"),
                QStringLiteral("--disallowedTools"), QStringLiteral("Bash,Edit,Write,NotebookEdit"),
                brief};
    }
    return {QStringLiteral("exec"), QStringLiteral("--sandbox"), QStringLiteral("read-only"),
            QStringLiteral("--skip-git-repo-check"), QStringLiteral("--ephemeral"),
            QStringLiteral("-o"), QStringLiteral("answer.txt"), brief};
}

void CloudEscalation::run(const QString &provider, const QString &brief)
{
    if (m_process) return;
    m_provider = provider;
    m_workDir = std::make_unique<QTemporaryDir>();
    m_process = new QProcess(this);
    m_process->setWorkingDirectory(m_workDir->path());
    connect(m_process, &QProcess::finished, this, [this](int code, QProcess::ExitStatus status) {
        const QString out = QString::fromUtf8(m_process->readAllStandardOutput());
        const QString err = QString::fromUtf8(m_process->readAllStandardError());
        m_process->deleteLater();
        m_process = nullptr;
        Q_EMIT runningChanged();
        if (status != QProcess::NormalExit || code != 0) {
            Q_EMIT failed(m_provider, (err.isEmpty() ? out : err).right(1500));
            return;
        }
        QString answer;
        if (m_provider == QLatin1String("claude")) {
            const QJsonObject o = QJsonDocument::fromJson(out.toUtf8()).object();
            answer = o.value(QStringLiteral("result")).toString();
            if (answer.isEmpty()) answer = out;
        } else {
            QFile f(m_workDir->filePath(QStringLiteral("answer.txt")));
            answer = f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : out;
        }
        Q_EMIT answered(m_provider, answer.trimmed());
    });
    connect(m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
        if (e != QProcess::FailedToStart || !m_process) return;
        m_process->deleteLater();
        m_process = nullptr;
        Q_EMIT runningChanged();
        Q_EMIT failed(m_provider, QStringLiteral("%1 could not be started").arg(m_provider));
    });
    const QString program = provider == QLatin1String("claude") ? m_claude : m_codex;
    m_process->start(program, arguments(provider, brief));
    m_process->closeWriteChannel();
    Q_EMIT runningChanged();
}

void CloudEscalation::cancel()
{
    if (m_process) m_process->kill();
}

} // namespace QindaPortage::Agent
