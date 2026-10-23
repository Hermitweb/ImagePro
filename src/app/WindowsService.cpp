#include "WindowsService.h"
#include "utils/Logger.h"

#ifdef Q_OS_WIN
#include <windows.h>
#include <winsvc.h>
#endif

#include <QCoreApplication>
#include <QFileInfo>
#include <QStringList>

namespace yingtu {

#ifdef Q_OS_WIN

namespace {

// 全局状态：ServiceMain 与控制处理器分属不同线程，靠它通信。
struct ServiceState {
    SERVICE_STATUS status{};
    SERVICE_STATUS_HANDLE handle = nullptr;
    std::function<void()> stopRequested;
    std::function<int()> worker;
};

ServiceState g_service;

DWORD WINAPI serviceControlHandler(DWORD control, DWORD, LPVOID, LPVOID)
{
    switch (control) {
    case SERVICE_CONTROL_STOP:
    case SERVICE_CONTROL_SHUTDOWN:
        if (g_service.stopRequested)
            g_service.stopRequested();
        g_service.status.dwCurrentState = SERVICE_STOP_PENDING;
        g_service.status.dwWaitHint = 30000; // 30 秒内应停止
        SetServiceStatus(g_service.handle, &g_service.status);
        break;
    case SERVICE_CONTROL_INTERROGATE:
        SetServiceStatus(g_service.handle, &g_service.status);
        break;
    default:
        break;
    }
    return NO_ERROR;
}

void WINAPI serviceMain(DWORD, LPWSTR*)
{
    g_service.status.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
    g_service.status.dwCurrentState = SERVICE_START_PENDING;
    g_service.status.dwWin32ExitCode = NO_ERROR;
    g_service.status.dwServiceSpecificExitCode = 0;
    g_service.status.dwWaitHint = 30000;

    g_service.handle = RegisterServiceCtrlHandlerExW(
        QStringLiteral("ImageProService").toStdWString().c_str(), serviceControlHandler, nullptr);
    if (!g_service.handle)
        return;

    g_service.status.dwCurrentState = SERVICE_RUNNING;
    g_service.status.dwWaitHint = 0;
    SetServiceStatus(g_service.handle, &g_service.status);

    int exitCode = 0;
    if (g_service.worker)
        exitCode = g_service.worker();

    g_service.status.dwCurrentState = SERVICE_STOPPED;
    g_service.status.dwWin32ExitCode = exitCode;
    g_service.status.dwWaitHint = 0;
    SetServiceStatus(g_service.handle, &g_service.status);
}

QString lastErrorMessage()
{
    return QString::fromLocal8Bit(qPrintable(QStringLiteral("系统错误 %1").arg(GetLastError())));
}

} // namespace

#endif // Q_OS_WIN

QString WindowsService::defaultServiceName()
{
    return QStringLiteral("ImageProService");
}

QString WindowsService::exePath()
{
    return QCoreApplication::applicationFilePath();
}

bool WindowsService::install(const QString& serviceName, const QString& displayName,
                             const QString& description, const QString& binaryPath, int startType,
                             QString* errorMessage)
{
#ifndef Q_OS_WIN
    Q_UNUSED(serviceName);
    Q_UNUSED(displayName);
    Q_UNUSED(description);
    Q_UNUSED(binaryPath);
    Q_UNUSED(startType);
    if (errorMessage)
        *errorMessage = QStringLiteral("Windows 服务仅在 Windows 上可用。");
    return false;
#else
    const QString exe = binaryPath.isEmpty() ? exePath() : binaryPath;

    // 服务的二进制路径必须带 "--service" 参数，并整体加引号：
    // 路径含空格时（默认装在 Program Files）不加引号会启动失败。
    QString commandLine = QStringLiteral("\"%1\" --service").arg(exe);

    const std::wstring name = serviceName.toStdWString();
    const std::wstring display = displayName.toStdWString();
    const std::wstring command = commandLine.toStdWString();

    SC_HANDLE manager = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CREATE_SERVICE);
    if (!manager) {
        if (errorMessage) {
            *errorMessage =
                QStringLiteral("无法打开服务管理器（需要管理员权限）：%1").arg(lastErrorMessage());
        }
        return false;
    }

    SC_HANDLE service =
        CreateServiceW(manager, name.c_str(), display.c_str(), SERVICE_ALL_ACCESS,
                       SERVICE_WIN32_OWN_PROCESS, startType, SERVICE_ERROR_NORMAL, command.c_str(),
                       nullptr, nullptr, nullptr, nullptr, nullptr);

    if (!service) {
        const DWORD error = GetLastError();
        if (error == ERROR_SERVICE_EXISTS) {
            // 已存在：更新显示信息与启动类型即可，不当作失败。
            SC_HANDLE existing = OpenServiceW(manager, name.c_str(), SERVICE_CHANGE_CONFIG);
            if (existing) {
                ChangeServiceConfigW(existing, SERVICE_WIN32_OWN_PROCESS, startType,
                                     SERVICE_ERROR_NORMAL, command.c_str(), nullptr, nullptr,
                                     nullptr, nullptr, nullptr, nullptr);
                CloseServiceHandle(existing);
                CloseServiceHandle(manager);
                if (errorMessage)
                    *errorMessage = QStringLiteral("服务已存在，已更新其配置。");
                return true;
            }
        }
        if (errorMessage) {
            *errorMessage = QStringLiteral("创建服务失败：%1").arg(lastErrorMessage());
        }
        CloseServiceHandle(manager);
        return false;
    }

    // 服务描述：显示在 services.msc 的「描述」列。
    SERVICE_DESCRIPTIONW desc;
    const std::wstring descriptionText = description.toStdWString();
    desc.lpDescription = const_cast<LPWSTR>(descriptionText.c_str());
    ChangeServiceConfig2W(service, SERVICE_CONFIG_DESCRIPTION, &desc);

    CloseServiceHandle(service);
    CloseServiceHandle(manager);

    Logger::info(QStringLiteral("已注册 Windows 服务：%1").arg(serviceName),
                 QStringLiteral("Service"));
    return true;
#endif
}

bool WindowsService::uninstall(const QString& serviceName, QString* errorMessage)
{
#ifndef Q_OS_WIN
    Q_UNUSED(serviceName);
    if (errorMessage)
        *errorMessage = QStringLiteral("Windows 服务仅在 Windows 上可用。");
    return false;
#else
    SC_HANDLE manager = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!manager) {
        if (errorMessage) {
            *errorMessage =
                QStringLiteral("无法打开服务管理器（需要管理员权限）：%1").arg(lastErrorMessage());
        }
        return false;
    }

    const std::wstring name = serviceName.toStdWString();
    SC_HANDLE service =
        OpenServiceW(manager, name.c_str(), DELETE | SERVICE_STOP | SERVICE_QUERY_STATUS);
    if (!service) {
        const DWORD error = GetLastError();
        CloseServiceHandle(manager);
        if (error == ERROR_SERVICE_DOES_NOT_EXIST) {
            if (errorMessage)
                *errorMessage = QStringLiteral("服务未注册，无需卸载。");
            return true; // 目标状态已达成
        }
        if (errorMessage)
            *errorMessage = QStringLiteral("打开服务失败：%1").arg(lastErrorMessage());
        return false;
    }

    // 先停再删：运行中的服务无法直接删除。
    SERVICE_STATUS status;
    ControlService(service, SERVICE_CONTROL_STOP, &status);
    DeleteService(service);
    CloseServiceHandle(service);
    CloseServiceHandle(manager);

    Logger::info(QStringLiteral("已卸载 Windows 服务：%1").arg(serviceName),
                 QStringLiteral("Service"));
    return true;
#endif
}

bool WindowsService::isInstalled(const QString& serviceName)
{
#ifndef Q_OS_WIN
    Q_UNUSED(serviceName);
    return false;
#else
    SC_HANDLE manager = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!manager)
        return false;
    const std::wstring name = serviceName.toStdWString();
    SC_HANDLE service = OpenServiceW(manager, name.c_str(), SERVICE_QUERY_STATUS);
    CloseServiceHandle(manager);
    if (!service)
        return false;
    CloseServiceHandle(service);
    return true;
#endif
}

bool WindowsService::isRunning(const QString& serviceName)
{
#ifndef Q_OS_WIN
    Q_UNUSED(serviceName);
    return false;
#else
    SC_HANDLE manager = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!manager)
        return false;
    const std::wstring name = serviceName.toStdWString();
    SC_HANDLE service = OpenServiceW(manager, name.c_str(), SERVICE_QUERY_STATUS);
    CloseServiceHandle(manager);
    if (!service)
        return false;

    SERVICE_STATUS status;
    const bool ok = QueryServiceStatus(service, &status);
    CloseServiceHandle(service);
    return ok && status.dwCurrentState == SERVICE_RUNNING;
#endif
}

QString WindowsService::statusText(const QString& serviceName)
{
#ifndef Q_OS_WIN
    Q_UNUSED(serviceName);
    return QString();
#else
    SC_HANDLE manager = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!manager)
        return QString();
    const std::wstring name = serviceName.toStdWString();
    SC_HANDLE service = OpenServiceW(manager, name.c_str(), SERVICE_QUERY_STATUS);
    CloseServiceHandle(manager);
    if (!service)
        return QString();

    SERVICE_STATUS status;
    QString text;
    if (QueryServiceStatus(service, &status)) {
        switch (status.dwCurrentState) {
        case SERVICE_RUNNING:
            text = QStringLiteral("正在运行");
            break;
        case SERVICE_STOPPED:
            text = QStringLiteral("已停止");
            break;
        case SERVICE_START_PENDING:
            text = QStringLiteral("正在启动");
            break;
        case SERVICE_STOP_PENDING:
            text = QStringLiteral("正在停止");
            break;
        default:
            text = QStringLiteral("状态码 %1").arg(status.dwCurrentState);
            break;
        }
    }
    CloseServiceHandle(service);
    return text;
#endif
}

int WindowsService::run(const QString& name, const std::function<int()>& worker,
                        const std::function<void()>& onStopRequested)
{
#ifndef Q_OS_WIN
    Q_UNUSED(name);
    Q_UNUSED(worker);
    Q_UNUSED(onStopRequested);
    return 1;
#else
    // 控制处理器注册的是固定名字的 ServiceMain；服务名仅用于日志与排障。
    Q_UNUSED(name);
    g_service.worker = worker;
    g_service.stopRequested = onStopRequested;

    SERVICE_TABLE_ENTRYW table[] = {{const_cast<LPWSTR>(L"ImageProService"), serviceMain},
                                    {nullptr, nullptr}};

    if (!StartServiceCtrlDispatcherW(table)) {
        Logger::error(QStringLiteral("无法连接服务控制管理器：%1").arg(lastErrorMessage()),
                      QStringLiteral("Service"));
        return 1;
    }
    return 0;
#endif
}

} // namespace yingtu
