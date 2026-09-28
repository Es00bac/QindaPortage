// SPDX-License-Identifier: GPL-3.0-or-later
#include "agent/llmruntime.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QTcpServer>

namespace QindaPortage::Agent {

namespace {

constexpr qint64 GiB = 1024LL * 1024 * 1024;

// AGENT-NOTE: ranks and headroom for the models this machine class runs
// (docs/design.md "Model choice"). Headroom covers a 16k KV cache (Qwen3.5's
// hybrid attention keeps it small), Vulkan compute buffers and 1.5 GiB left
// for the desktop so loading never pushes the laptop into swap.
LocalModel describe(const QFileInfo &file)
{
    LocalModel m;
    m.path = file.absoluteFilePath();
    m.sizeBytes = file.size();
    const QString name = file.fileName();
    m.label = file.completeBaseName();
    m.id = file.completeBaseName().toLower();
    m.headroomBytes = qint64(2.5 * GiB);
    if (name.startsWith(QLatin1String("Qwen3.5-9B"))) {
        m.id = QStringLiteral("qwen3.5-9b");
        m.label = QStringLiteral("Qwen3.5-9B");
        m.rank = 90;
    } else if (name.startsWith(QLatin1String("Qwen3.5-4B"))) {
        m.id = QStringLiteral("qwen3.5-4b");
        m.label = QStringLiteral("Qwen3.5-4B");
        m.rank = 50;
        m.headroomBytes = qint64(2.0 * GiB);
    } else if (name.contains(QLatin1String("mmproj"))) {
        m.rank = -1; // vision projector, not a chat model
    }
    return m;
}

int freeLoopbackPort()
{
    QTcpServer probe;
    if (!probe.listen(QHostAddress::LocalHost, 0)) return 18990;
    return probe.serverPort();
}

} // namespace

LlmRuntime::LlmRuntime(QObject *parent)
    : QObject(parent)
    , m_net(new QNetworkAccessManager(this))
{
    m_healthTimer.setInterval(400);
    connect(&m_healthTimer, &QTimer::timeout, this, &LlmRuntime::pollHealth);
    m_idleTimer.setSingleShot(true);
    connect(&m_idleTimer, &QTimer::timeout, this, [this] {
        m_unloadReason = QStringLiteral("idle for %1 s").arg(m_idleSeconds);
        unload();
    });
    m_memoryTimer.setInterval(5000);
    connect(&m_memoryTimer, &QTimer::timeout, this, [this] {
        m_available = memAvailableBytes();
        Q_EMIT memoryChanged();
    });
    m_memoryTimer.start();
    m_available = memAvailableBytes();
    scanModels();
}

LlmRuntime::~LlmRuntime()
{
    if (m_process) {
        m_process->disconnect(this);
        m_process->terminate();
        if (!m_process->waitForFinished(3000)) m_process->kill();
    }
}

void LlmRuntime::scanModels(const QStringList &dirs)
{
    m_models.clear();
    for (const QString &dir : dirs) {
        for (const QFileInfo &f : QDir(dir).entryInfoList({QStringLiteral("*.gguf")}, QDir::Files)) {
            LocalModel m = describe(f);
            if (m.rank >= 0) m_models.append(m);
        }
    }
    std::sort(m_models.begin(), m_models.end(), [](const auto &a, const auto &b) { return a.rank > b.rank; });
    Q_EMIT settingsChanged();
}

qint64 LlmRuntime::memAvailableBytes()
{
    QFile f(QStringLiteral("/proc/meminfo"));
    if (!f.open(QIODevice::ReadOnly)) return 0;
    while (!f.atEnd()) {
        const QByteArray line = f.readLine();
        if (line.startsWith("MemAvailable:")) {
            return line.mid(13).trimmed().split(' ').value(0).toLongLong() * 1024;
        }
    }
    return 0;
}

const LocalModel *LlmRuntime::chooseModel(qint64 availableBytes) const
{
    if (!m_preferred.isEmpty()) {
        for (const LocalModel &m : m_models) {
            if (m.id == m_preferred) return &m;
        }
    }
    for (const LocalModel &m : m_models) {
        if (m.sizeBytes + m.headroomBytes <= availableBytes) return &m;
    }
    // Nothing fits comfortably: the smallest model is still better than none.
    return m_models.isEmpty() ? nullptr : &m_models.last();
}

QString LlmRuntime::stateName() const
{
    switch (m_state) {
    case State::Unloaded: return QStringLiteral("unloaded");
    case State::Loading: return QStringLiteral("loading");
    case State::Ready: return QStringLiteral("ready");
    case State::Unloading: return QStringLiteral("unloading");
    case State::Failed: return QStringLiteral("failed");
    }
    return {};
}

QUrl LlmRuntime::baseUrl() const
{
    return QUrl(QStringLiteral("http://127.0.0.1:%1/v1").arg(m_port));
}

void LlmRuntime::setIdleSeconds(int s)
{
    if (s == m_idleSeconds) return;
    m_idleSeconds = std::max(30, s);
    Q_EMIT settingsChanged();
}

void LlmRuntime::setPreferredModel(const QString &id)
{
    if (id == m_preferred) return;
    m_preferred = id;
    Q_EMIT settingsChanged();
}

QVariantList LlmRuntime::modelList() const
{
    QVariantList out;
    for (const LocalModel &m : m_models) {
        out.append(QVariantMap{{QStringLiteral("id"), m.id}, {QStringLiteral("label"), m.label},
                               {QStringLiteral("gib"), m.sizeBytes / double(GiB)},
                               {QStringLiteral("needsGiB"), (m.sizeBytes + m.headroomBytes) / double(GiB)}});
    }
    return out;
}

void LlmRuntime::setServerCommand(const QString &program, const QStringList &extraArgs)
{
    m_program = program;
    m_extraArgs = extraArgs;
}

void LlmRuntime::setState(State s)
{
    if (s == m_state) return;
    m_state = s;
    Q_EMIT stateChanged();
}

void LlmRuntime::ensureLoaded()
{
    touch();
    if (m_state == State::Ready) {
        Q_EMIT ready();
        return;
    }
    if (m_state == State::Loading) return;
    m_available = memAvailableBytes();
    const LocalModel *model = chooseModel(m_available);
    if (!model) {
        m_error = QStringLiteral("no local model installed in /usr/share/gguf");
        setState(State::Failed);
        Q_EMIT failed(m_error);
        return;
    }
    m_active = *model;
    m_error.clear();
    m_port = freeLoopbackPort();
    // AGENT-NOTE: thinking stays off by default (a reasoning trace costs
    // minutes on the iGPU); requests turn it on per call through
    // chat_template_kwargs. --jinja is required for tool calls. One slot so
    // the prompt cache follows the single conversation.
    QStringList args{QStringLiteral("--host"), QStringLiteral("127.0.0.1"),
                     QStringLiteral("--port"), QString::number(m_port),
                     QStringLiteral("--model"), m_active.path,
                     QStringLiteral("--alias"), m_active.id,
                     QStringLiteral("--ctx-size"), QStringLiteral("16384"),
                     QStringLiteral("--n-gpu-layers"), QStringLiteral("99"),
                     QStringLiteral("--parallel"), QStringLiteral("1"),
                     QStringLiteral("--jinja"),
                     QStringLiteral("--chat-template-kwargs"), QStringLiteral("{\"enable_thinking\":false}")};
    args += m_extraArgs;
    m_process = new QProcess(this);
    m_process->setProcessChannelMode(QProcess::MergedChannels);
    connect(m_process, &QProcess::finished, this, &LlmRuntime::onFinished);
    connect(m_process, &QProcess::readyRead, this, [this] {
        // Keep only the tail for error reports.
        const QString text = QString::fromUtf8(m_process->readAll());
        m_error = (m_error + text).right(2000);
    });
    setState(State::Loading);
    m_loadClock.start();
    m_process->start(m_program, args);
    m_healthTimer.start();
}

void LlmRuntime::pollHealth()
{
    if (m_state != State::Loading) {
        m_healthTimer.stop();
        return;
    }
    if (m_loadClock.elapsed() > 180000) {
        m_error = QStringLiteral("model did not load within 3 minutes");
        m_unloadReason = m_error;
        unload();
        setState(State::Failed);
        Q_EMIT failed(m_error);
        return;
    }
    QNetworkRequest request(QUrl(QStringLiteral("http://127.0.0.1:%1/health").arg(m_port)));
    request.setTransferTimeout(1000);
    QNetworkReply *reply = m_net->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        if (m_state == State::Loading && reply->error() == QNetworkReply::NoError
            && reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 200) {
            m_healthTimer.stop();
            m_error.clear();
            m_available = memAvailableBytes();
            setState(State::Ready);
            touch();
            Q_EMIT memoryChanged();
            Q_EMIT ready();
        }
    });
}

void LlmRuntime::touch()
{
    if (m_state == State::Ready) m_idleTimer.start(m_idleSeconds * 1000);
}

void LlmRuntime::unload()
{
    m_idleTimer.stop();
    m_healthTimer.stop();
    if (!m_process) {
        setState(State::Unloaded);
        return;
    }
    if (m_unloadReason.isEmpty()) m_unloadReason = QStringLiteral("requested");
    setState(State::Unloading);
    m_process->terminate();
    QProcess *p = m_process;
    QTimer::singleShot(4000, p, [p] {
        if (p->state() != QProcess::NotRunning) p->kill();
    });
}

void LlmRuntime::onFinished(int code, QProcess::ExitStatus status)
{
    const bool expected = m_state == State::Unloading;
    m_process->deleteLater();
    m_process = nullptr;
    const QString reason = m_unloadReason;
    m_unloadReason.clear();
    if (expected) {
        m_error.clear();
        setState(State::Unloaded);
        m_available = memAvailableBytes();
        Q_EMIT memoryChanged();
        Q_EMIT unloaded(reason);
        return;
    }
    // Crashed or exited on its own (bad model file, out of memory).
    m_error = QStringLiteral("llama-server exited (%1, code %2): %3")
                  .arg(status == QProcess::CrashExit ? QStringLiteral("crash") : QStringLiteral("exit"))
                  .arg(code)
                  .arg(m_error.right(400).trimmed());
    setState(State::Failed);
    Q_EMIT failed(m_error);
}

} // namespace QindaPortage::Agent
