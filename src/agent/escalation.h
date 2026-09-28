// SPDX-License-Identifier: GPL-3.0-or-later
//
// Tier 3: ask Claude (`claude -p`) or Codex (`codex exec`) through the CLIs
// the operator is already signed in to. Only the brief the user approved
// is sent; the CLIs run with no shell or file-editing tools, and the answer
// comes back as advice that the local session validates with its own tools
// before anything reaches a plan (docs/design.md "Escalation", safety rule 4).

#pragma once

#include <QObject>
#include <QProcess>
#include <QTemporaryDir>

#include <memory>

namespace QindaPortage::Agent {

class CloudEscalation final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool running READ running NOTIFY runningChanged)
    Q_PROPERTY(QStringList providers READ providers CONSTANT)

public:
    explicit CloudEscalation(QObject *parent = nullptr);

    // "claude" and/or "codex", whichever CLI is on PATH.
    QStringList providers() const;
    bool running() const { return m_process != nullptr; }

    // The brief shown to the user for approval. `transcript` is the session
    // transcript (QVariantList of entries); `plan` the draft plan text.
    Q_INVOKABLE static QString buildBrief(const QString &goal, const QString &systemProfile,
                                          const QVariantList &transcript, const QString &plan);

    // Test seam: replaces the program for a provider.
    void setProgram(const QString &provider, const QString &program);
    // The argument list used for a provider (exposed for tests).
    QStringList arguments(const QString &provider, const QString &brief) const;

public Q_SLOTS:
    void run(const QString &provider, const QString &brief);
    void cancel();

Q_SIGNALS:
    void runningChanged();
    void answered(const QString &provider, const QString &text);
    void failed(const QString &provider, const QString &error);

private:
    QProcess *m_process = nullptr;
    std::unique_ptr<QTemporaryDir> m_workDir;
    QString m_provider;
    QString m_claude = QStringLiteral("claude");
    QString m_codex = QStringLiteral("codex");
};

} // namespace QindaPortage::Agent
