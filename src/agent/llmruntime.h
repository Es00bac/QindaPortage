// SPDX-License-Identifier: GPL-3.0-or-later
//
// LlmRuntime owns the local llama-server child process: it picks the largest
// installed model that fits in the memory available *now*, loads it on
// demand, and unloads it when idle or when a plan is about to build, so the
// compiler gets the RAM and the iGPU (docs/design.md "Model lifecycle").

#pragma once

#include <QElapsedTimer>
#include <QObject>
#include <QProcess>
#include <QTimer>
#include <QUrl>
#include <QVariantList>

class QNetworkAccessManager;

namespace QindaPortage::Agent {

struct LocalModel
{
    QString id;          // alias sent as "model"
    QString label;       // "Qwen3.5-9B (Q4_K_XL)"
    QString path;
    qint64 sizeBytes = 0;
    int rank = 0;        // higher = more capable
    // RAM that must be available beyond the weights (KV cache, compute
    // buffers, headroom for the desktop).
    qint64 headroomBytes = 0;
};

class LlmRuntime final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString state READ stateName NOTIFY stateChanged)
    Q_PROPERTY(QString modelLabel READ modelLabel NOTIFY stateChanged)
    Q_PROPERTY(QString modelId READ modelId NOTIFY stateChanged)
    Q_PROPERTY(QString error READ error NOTIFY stateChanged)
    Q_PROPERTY(double modelGiB READ modelGiB NOTIFY stateChanged)
    Q_PROPERTY(double availableGiB READ availableGiB NOTIFY memoryChanged)
    Q_PROPERTY(int idleSeconds READ idleSeconds WRITE setIdleSeconds NOTIFY settingsChanged)
    Q_PROPERTY(QVariantList models READ modelList NOTIFY settingsChanged)
    Q_PROPERTY(QString preferredModel READ preferredModel WRITE setPreferredModel NOTIFY settingsChanged)

public:
    enum class State { Unloaded, Loading, Ready, Unloading, Failed };
    Q_ENUM(State)

    explicit LlmRuntime(QObject *parent = nullptr);
    ~LlmRuntime() override;

    // Models found in `dirs` (default /usr/share/gguf) that this runtime
    // knows how to size; unknown GGUFs are listed with rank 0.
    void scanModels(const QStringList &dirs = {QStringLiteral("/usr/share/gguf")});
    const QVector<LocalModel> &models() const { return m_models; }

    // The model load() would pick right now, or nullptr if none fits.
    const LocalModel *chooseModel(qint64 availableBytes) const;
    static qint64 memAvailableBytes();

    State state() const { return m_state; }
    QString stateName() const;
    QString modelLabel() const { return m_active.label; }
    QString modelId() const { return m_active.id; }
    double modelGiB() const { return m_active.sizeBytes / 1073741824.0; }
    double availableGiB() const { return m_available / 1073741824.0; }
    QString error() const { return m_error; }
    QUrl baseUrl() const;
    int idleSeconds() const { return m_idleSeconds; }
    void setIdleSeconds(int s);
    QString preferredModel() const { return m_preferred; }
    void setPreferredModel(const QString &id);
    QVariantList modelList() const;

    // Test seam: the server binary and extra arguments.
    void setServerCommand(const QString &program, const QStringList &extraArgs = {});

public Q_SLOTS:
    // Starts the server if needed; emits ready() (possibly immediately) or
    // failed().
    void ensureLoaded();
    // Stops the server now. AGENT-CONTRACT: Ops::Executor::aboutToStart is
    // connected to this in main.cpp; the helper must never build while the
    // model holds its memory.
    void unload();
    // Call on every request so the idle timer restarts.
    void touch();

Q_SIGNALS:
    void stateChanged();
    void memoryChanged();
    void settingsChanged();
    void ready();
    void failed(const QString &error);
    void unloaded(const QString &reason);

private:
    void setState(State s);
    void pollHealth();
    void onFinished(int code, QProcess::ExitStatus status);

    QVector<LocalModel> m_models;
    LocalModel m_active;
    State m_state = State::Unloaded;
    QString m_error;
    QString m_preferred;
    QString m_program = QStringLiteral("llama-server");
    QStringList m_extraArgs;
    QProcess *m_process = nullptr;
    QNetworkAccessManager *m_net = nullptr;
    QTimer m_healthTimer;
    QTimer m_idleTimer;
    QTimer m_memoryTimer;
    QElapsedTimer m_loadClock;
    QString m_unloadReason;
    qint64 m_available = 0;
    int m_port = 0;
    int m_idleSeconds = 180;
};

} // namespace QindaPortage::Agent
