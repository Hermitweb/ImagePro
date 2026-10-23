#pragma once

#include <QObject>
#include <QString>

class QLocalServer;
class QLockFile;

namespace yingtu {

/**
 * 单实例守卫。
 *
 * 分工明确，两个机制各司其职：
 *
 *   * **QLockFile —— 判定「有没有别人在跑」**
 *     用原子文件锁（Windows 上是 CREATE_NEW 语义）实现，只有一个进程能拿到锁。
 *     这在所有平台上都是可靠的。
 *
 *   * **QLocalServer —— 传递「请把窗口显示到前台」的请求**
 *     新实例启动时把请求发给已有实例，用户体验上等同于
 *     「点击了图标但窗口没动」被正确处理，而不是弹出第二个窗口。
 *
 * 为什么不用 QLocalServer 单独判定单实例：在 Windows 上
 * QLocalServer 底层是命名管道，CreateNamedPipe 对已存在的管道名
 * **允许**再次创建（同一管道的多个实例），因此第二个进程 listen()
 * 依然会成功，根本检测不到冲突。QLockFile 没有这个问题。
 *
 * 陈旧锁的处理：设置 staleLockTime(0)，即「不按时间判定陈旧」，
 * 只按「记录下来的进程是否还活着」判定。这样运行数小时的实例不会
 * 被误判为陈旧，而崩溃残留的锁会因进程已死而被自动接管。
 */
class SingleInstanceGuard : public QObject
{
    Q_OBJECT
public:
    /**
     * @param instanceName 实例标识。为空时使用默认名（按用户区分）。
     *        测试或多份并存安装时传入独立名字，避免互相抢占同一个锁。
     */
    explicit SingleInstanceGuard(QObject* parent = nullptr,
                                 const QString& instanceName = QString());
    ~SingleInstanceGuard() override;

    /**
     * 尝试成为主实例。
     * @return true  —— 本进程是唯一实例，可以正常启动主窗口；
     *         false —— 已有实例在运行，且已尽力把激活请求发了过去，
     *                   调用方应立即退出（返回 0）。
     */
    bool tryBecomePrimary();

signals:
    /** 已有实例收到新实例的激活请求时应发出此信号。 */
    void activationRequested();

private:
    void handleNewConnection();

    /**
     * 向已有实例发送「请显示窗口」请求。
     *
     * 带重试：持锁者可能刚拿到锁、还没开始监听，
     * 此时一次 connect 会失败，需要给一点时间。
     *
     * @return true 表示联系成功并已发出请求。
     */
    bool notifyExistingInstance(const QString& name);

    QString serverName() const;
    QString lockFilePath() const;
    /** 实例标识：构造时指定，或取环境变量 IMAGEPRO_INSTANCE。 */
    QString instanceName() const;

    QLocalServer* m_server = nullptr;
    QLockFile* m_lockFile = nullptr;
    // 显式指定的实例标识；为空时回退到环境变量。
    QString m_instanceName;
    // 仅当本进程真正监听了套接字时，析构才允许摘除它。
    // 否则非主实例退出会把主实例的套接字一并删掉，导致下次启动失去激活能力。
    bool m_isPrimary = false;
    // 仅当本进程真的拿到了锁时才允许 unlock()：
    // QLockFile::unlock() 会无条件删除锁文件，若非持锁者调用它，
    // 会把主实例的锁文件删掉，单实例保护随之失效。
    bool m_hasLock = false;
};

} // namespace yingtu
