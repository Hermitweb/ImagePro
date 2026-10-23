#include "app/SingleInstanceGuard.h"

#include <QCoreApplication>
#include <QSignalSpy>
#include <QTest>

using namespace yingtu;

// 单实例守卫的回归保护。
//
// 覆盖两个容易写错、且都会导致实际故障的点：
//   1. 第二个实例必须被拒绝，并且把「显示窗口」的请求送到主实例；
//   2. 非主实例退出时不得摘除主实例的套接字 —— 否则主实例失去单实例保护，
//      下次启动会再拉起第二个窗口（这是最典型的「重启后又变成两个窗口」故障）。
//
// 注意：本用例占用真实的本地套接字名，若机器上已运行着影图，本用例会失败。
// CI 环境不会预先启动应用，因此是安全的。

class TestApp : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void testFirstInstanceBecomesPrimary();
    void testSecondInstanceIsRejectedAndNotifiesPrimary();
    void testNonPrimaryExitKeepsPrimaryGuard();

private:
    // 本次测试使用的实例标识。必须唯一：否则与另一个构建目录里的
    // test_app 并发运行时，会共用同一个锁文件与本地套接字而互相干扰。
    QString m_instanceName;
};

void TestApp::initTestCase()
{
    m_instanceName = QStringLiteral("test-%1").arg(QCoreApplication::applicationPid());
    qputenv("IMAGEPRO_INSTANCE", m_instanceName.toUtf8());
}

void TestApp::cleanupTestCase()
{
    qunsetenv("IMAGEPRO_INSTANCE");
}

void TestApp::testFirstInstanceBecomesPrimary()
{
    SingleInstanceGuard guard;
    QVERIFY2(guard.tryBecomePrimary(), "the first instance should become the primary one");
}

void TestApp::testSecondInstanceIsRejectedAndNotifiesPrimary()
{
    SingleInstanceGuard primary;
    QVERIFY(primary.tryBecomePrimary());

    QSignalSpy spy(&primary, &SingleInstanceGuard::activationRequested);

    SingleInstanceGuard secondary;
    QVERIFY2(!secondary.tryBecomePrimary(),
             "a second instance must be rejected while the primary is running");

    // 主实例应收到激活请求（需要事件循环把套接字连接派发过来）。
    QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 3000);
}

void TestApp::testNonPrimaryExitKeepsPrimaryGuard()
{
    SingleInstanceGuard primary;
    QVERIFY(primary.tryBecomePrimary());

    {
        // 非主实例：listen 失败 → 走「通知已有实例」分支。
        SingleInstanceGuard secondary;
        QVERIFY(!secondary.tryBecomePrimary());
        // secondary 在此析构。若它错误地调用了 QLocalServer::removeServer()，
        // 主实例的套接字会被摘掉。
    }

    // 主实例的套接字必须仍然存在：再起一个实例仍应被拒绝。
    SingleInstanceGuard third;
    QVERIFY2(!third.tryBecomePrimary(),
             "primary guard must survive the exit of a non-primary instance");
}

QTEST_MAIN(TestApp)
#include "test_app.moc"
