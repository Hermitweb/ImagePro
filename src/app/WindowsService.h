#pragma once

#include <QString>
#include <functional>

namespace yingtu {

/**
 * Windows 服务（Service Control Manager）封装。
 *
 * 用 Win32 SCM API 直接实现，没有依赖 Qt5 的 QServiceBase ——
 * MSYS2 的 Qt5 并不提供该头文件，而服务本身只需要几个 SCM 调用。
 *
 * 三种模式（由命令行参数触发）：
 *   --install-service    注册服务（需要管理员权限）
 *   --uninstall-service  卸载服务（需要管理员权限）
 *   --service            以服务方式运行
 */
class WindowsService
{
public:
    /** 服务名（SC 内部名），全局唯一。 */
    static QString defaultServiceName();

    /**
     * 注册服务。
     * @param binaryPath 服务可执行文件路径；留空则用当前 exe。
     * @param startType  SERVICE_AUTO_START / SERVICE_DEMAND_START。
     * @return 成功返回 true；失败时 errorMessage 给出原因。
     */
    static bool install(const QString& serviceName, const QString& displayName,
                        const QString& description, const QString& binaryPath, int startType,
                        QString* errorMessage);

    /** 卸载服务（连同已注册的服务项）。 */
    static bool uninstall(const QString& serviceName, QString* errorMessage);

    /** 服务是否已注册。 */
    static bool isInstalled(const QString& serviceName);

    /** 服务当前是否正在运行。 */
    static bool isRunning(const QString& serviceName);

    /** 服务状态描述（未安装时返回空）。 */
    static QString statusText(const QString& serviceName);

    /**
     * 以服务方式运行。
     *
     * @param name    服务名
     * @param worker  服务主循环；返回进程退出码。
     *                必须在 ServiceMain 线程里阻塞运行，StopService 会通过
     *                stopRequested 通知它退出。
     * @param stopRequested 被请求停止时会被置位（由控制处理器设置）。
     */
    static int run(const QString& name, const std::function<int()>& worker,
                   const std::function<void()>& onStopRequested);

private:
    static QString exePath();
};

} // namespace yingtu
