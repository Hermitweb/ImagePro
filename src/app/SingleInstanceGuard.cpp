#include "SingleInstanceGuard.h"
#include "utils/Logger.h"

#include <QLocalServer>
#include <QLocalSocket>
#include <QLockFile>
#include <QStandardPaths>
#include <QThread>

#include <cstring>

namespace yingtu {

namespace {

// 激活请求的消息负载。保持极短：套接字只需传递一个意图，不需要传参。
constexpr const char* kActivateCommand = "ACTIVATE";

// 联系已有实例时的重试参数：持锁者可能刚拿到锁、还没开始监听。
constexpr int kNotifyAttempts = 10;
constexpr int kNotifyIntervalMs = 100;

} // namespace

SingleInstanceGuard::SingleInstanceGuard(QObject* parent, const QString& instanceName)
    : QObject(parent), m_instanceName(instanceName)
{}

SingleInstanceGuard::~SingleInstanceGuard()
{
    if (m_server) {
        m_server->close();
        // 只摘除自己监听的套接字。若本进程并非主实例（listen 失败），
        // 摘除的是主实例的套接字，会让后续启动失去激活能力。
        if (m_isPrimary)
            QLocalServer::removeServer(serverName());
        delete m_server;
        m_server = nullptr;
    }
    if (m_lockFile) {
        // 只在真的持有锁时解锁：QLockFile::unlock() 会无条件删除锁文件，
        // 非持锁者调用会把主实例的锁文件删掉。
        if (m_hasLock)
            m_lockFile->unlock();
        delete m_lockFile;
        m_lockFile = nullptr;
    }
}

QString SingleInstanceGuard::instanceName() const
{
    if (!m_instanceName.isEmpty())
        return m_instanceName;
    // 环境变量用于隔离：单元测试并行跑、或同时装多份时互不抢占。
    return qEnvironmentVariable("IMAGEPRO_INSTANCE");
}

QString SingleInstanceGuard::serverName() const
{
    // 带用户标识：同一台机器上不同用户的应用互不干扰。
    QString tag = QStandardPaths::writableLocation(QStandardPaths::HomeLocation);
    tag.replace(QChar('/'), QChar('_'));
    tag.replace(QChar('\\'), QChar('_'));
    tag.replace(QChar(':'), QChar('_'));
    if (tag.isEmpty())
        tag = QStringLiteral("default");
    const QString custom = instanceName();
    if (!custom.isEmpty())
        tag += QLatin1Char('-') + custom;
    return QStringLiteral("yingtu-imagepro-") + tag;
}

QString SingleInstanceGuard::lockFilePath() const
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    return dir + QLatin1Char('/') + serverName() + QStringLiteral(".lock");
}

bool SingleInstanceGuard::tryBecomePrimary()
{
    // ── 第一步：用原子文件锁判定单实例 ────────────────────────────
    m_lockFile = new QLockFile(lockFilePath());
    m_lockFile->setStaleLockTime(0); // 只按「进程是否还活着」判定陈旧

    if (!m_lockFile->tryLock(0)) {
        // 已有实例在运行：把「显示窗口」的请求发过去，然后本进程退出。
        const bool notified = notifyExistingInstance(serverName());
        Logger::info(
            QStringLiteral("Another instance is already running; "
                           "activation request %1.")
                .arg(notified ? QStringLiteral("sent") : QStringLiteral("could not be delivered")),
            QStringLiteral("SingleInstance"));
        return false;
    }
    m_hasLock = true;

    // ── 第二步：成为主实例，开始监听激活通道 ──────────────────────
    const QString name = serverName();

    // 清理上次异常退出残留的套接字（锁能拿到说明没有活着的实例）。
    QLocalServer::removeServer(name);

    m_server = new QLocalServer(this);
    if (!m_server->listen(name)) {
        // 监听失败不影响单实例保护（锁已经拿到了），只是无法接收激活请求。
        Logger::warning(
            QStringLiteral("Failed to listen on local server: %1").arg(m_server->errorString()),
            QStringLiteral("SingleInstance"));
        delete m_server;
        m_server = nullptr;
        return true;
    }

    connect(m_server, &QLocalServer::newConnection, this,
            &SingleInstanceGuard::handleNewConnection);
    m_isPrimary = true;

    Logger::info(QStringLiteral("Single-instance guard active: %1").arg(name),
                 QStringLiteral("SingleInstance"));
    return true;
}

bool SingleInstanceGuard::notifyExistingInstance(const QString& name)
{
    for (int attempt = 0; attempt < kNotifyAttempts; ++attempt) {
        QLocalSocket socket;
        socket.connectToServer(name);
        if (socket.waitForConnected(500)) {
            socket.write(kActivateCommand, qint64(qstrlen(kActivateCommand)));
            socket.flush();
            socket.waitForBytesWritten(500);
            socket.disconnectFromServer();
            return true;
        }
        // 持锁者可能还没开始监听，稍等再试。
        QThread::msleep(kNotifyIntervalMs);
    }
    return false;
}

void SingleInstanceGuard::handleNewConnection()
{
    while (m_server && m_server->hasPendingConnections()) {
        QLocalSocket* socket = m_server->nextPendingConnection();
        if (!socket)
            continue;
        // 读掉请求内容（内容本身不参与判断，真正的意图是「有人来了」）。
        socket->read(64);
        socket->disconnectFromServer();
        socket->deleteLater();

        emit activationRequested();
    }
}

} // namespace yingtu
