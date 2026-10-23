#include "app/AutoProcessService.h"
#include "app/ImageProApp.h"
#include "app/SingleInstanceGuard.h"
#include "app/WindowsService.h"
#include "ui/MainWindow.h"
#include "utils/Logger.h"

#include <QCoreApplication>
#include <QMessageBox>
#include <QString>
#include <QStringList>
#include <QTextStream>
#include <exception>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
// 顺序要求：shellapi.h 依赖 windows.h 里的 EXTERN_C 等宏，必须在其之后包含。
#include <windows.h>
#include <shellapi.h>
#endif

#ifdef WITH_VELOPACK
#include <Velopack.hpp>
#endif

namespace {

/**
 * 当前进程是否以管理员身份运行。
 * 只有注册/卸载 Windows 服务需要提权，其余操作一律普通权限。
 */
bool isAdministrator()
{
#ifdef Q_OS_WIN
    SID_IDENTIFIER_AUTHORITY ntAuthority = SECURITY_NT_AUTHORITY;
    PSID administrators = nullptr;
    if (!AllocateAndInitializeSid(&ntAuthority, 2, SECURITY_BUILTIN_DOMAIN_RID,
                                  DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0, 0, &administrators))
        return false;
    BOOL isAdmin = FALSE;
    CheckTokenMembership(nullptr, administrators, &isAdmin);
    FreeSid(administrators);
    return isAdmin == TRUE;
#else
    return false;
#endif
}

/**
 * 以管理员身份重新启动自身（触发 UAC 提示），用于注册/卸载服务。
 * @return true 表示已成功拉起提权进程，当前进程应当退出。
 */
bool relaunchElevated(const QStringList& arguments)
{
#ifdef Q_OS_WIN
    const QString exePath = QCoreApplication::applicationFilePath();
    QString parameters;
    for (const QString& argument : arguments)
        parameters += QStringLiteral("\"%1\" ").arg(argument);

    const std::wstring verb = L"runas";
    const std::wstring exe = exePath.toStdWString();
    const std::wstring params = parameters.toStdWString();
    const HINSTANCE result =
        ShellExecuteW(nullptr, verb.c_str(), exe.c_str(), params.c_str(), nullptr, SW_SHOWNORMAL);
    return reinterpret_cast<INT_PTR>(result) > 32;
#else
    Q_UNUSED(arguments);
    return false;
#endif
}

/**
 * 固定控制台输出编码为 UTF-8。
 *
 * 默认情况下 QTextStream 用系统 ANSI 代码页（中文 Windows 上是 GBK），
 * 而源码里的字符串是 UTF-8：两者不一致会让 --help / --service-status
 * 在控制台显示成乱码。Windows 10/11 的终端已普遍支持 UTF-8，
 * 显式设置可消除这类不确定性。
 */
void setupConsoleOutput()
{
#ifdef Q_OS_WIN
    // 本程序是 GUI 子系统（WIN32）编译的：默认**没有**自己的控制台，
    // 没有控制台时命令行自检（--help / --version / --service-status）
    // 的输出无处可去。挂到调用方（父进程）的控制台上。
    if (AttachConsole(ATTACH_PARENT_PROCESS) == FALSE) {
        AllocConsole();
    }

    // 只有在标准输出句柄**无效**时，才把 CRT 的 stdout 重新绑到控制台。
    // 若 stdout 已被重定向到管道或文件（命令行里的 > 与 |），句柄是有效的，
    // 必须保留 —— 无条件 freopen 会把重定向目标丢掉，
    // 输出反而进不了管道（`ImagePro.exe --version > ver.txt` 得到空文件）。
    const HANDLE stdOut = GetStdHandle(STD_OUTPUT_HANDLE);
    if (stdOut == nullptr || stdOut == INVALID_HANDLE_VALUE) {
        FILE* stream = nullptr;
        static_cast<void>(freopen_s(&stream, "CONOUT$", "w", stdout));
    }

    SetConsoleOutputCP(CP_UTF8);
#endif
}

/**
 * 把文本按 UTF-8 字节写到标准输出。
 *
 * 不走 QTextStream(FILE*)：它在 Windows 上会把 QString 先转成 UTF-8，
 * 再把每个字节当成 Latin-1 码位转一次 —— 结果是**双重编码**
 * （“影”会写成 C3 A5 C2 BD C2 B1 而不是 E5 BD B1），
 * 命令行里看到的就是「影图」变成「å½¾å‹¾」。
 * 直接 QString::toUtf8() + fwrite() 只有一次编码，行为确定。
 */
void writeStdout(const QString& text)
{
    const QByteArray bytes = text.toUtf8();
    // 写入失败（控制台不可写）没有可处理的分支，显式忽略返回值。
    static_cast<void>(fwrite(bytes.constData(), 1, static_cast<size_t>(bytes.size()), stdout));
    static_cast<void>(fflush(stdout));
}

/** 打印命令行用法（--help / 参数错误时）。 */
void printUsage()
{
    setupConsoleOutput();
    writeStdout(QStringLiteral(
        "影图 ImagePro\n"
        "\n"
        "用法:\n"
        "  ImagePro.exe                     启动图形界面\n"
        "  ImagePro.exe --install-service   注册 Windows 服务（自动处理，需管理员权限）\n"
        "  ImagePro.exe --uninstall-service 卸载 Windows 服务（需管理员权限）\n"
        "  ImagePro.exe --service           以服务方式运行\n"
        "  ImagePro.exe --service-status    查询服务注册与运行状态\n"
        "  ImagePro.exe --version           显示版本号\n"
        "\n"
        "环境变量:\n"
        "  IMAGEPRO_CONFIG_DIR              配置目录（便携版指向程序目录）\n"
        "  IMAGEPRO_DATA_DIR                数据目录（便携版指向程序目录）\n"
        "  IMAGEPRO_INSTANCE                实例标识（多份并存时隔离单实例锁）\n"));
}

/** 查询服务状态（只输出到标准输出，不需要 GUI）。 */
void printServiceStatus()
{
    setupConsoleOutput();
    const QString serviceName = yingtu::WindowsService::defaultServiceName();
    const bool installed = yingtu::WindowsService::isInstalled(serviceName);
    QString text = QStringLiteral("service: %1\ninstalled: %2\nrunning: %3\n")
                       .arg(serviceName, installed ? QStringLiteral("yes") : QStringLiteral("no"),
                            yingtu::WindowsService::isRunning(serviceName) ? QStringLiteral("yes")
                                                                           : QStringLiteral("no"));
    if (installed) {
        const QString state = yingtu::WindowsService::statusText(serviceName);
        if (!state.isEmpty())
            text += QStringLiteral("state: %1\n").arg(state);
    }
    writeStdout(text);
}
} // namespace

int main(int argc, char* argv[])
{
    // SetDefaultDllDirectories 把 DLL 搜索路径限制为程序目录 + System32，避免用户
    // PATH 中同名但不兼容的 DLL 被加载。返回值此前被忽略——失败时这项加固就静默
    // 失效了。这里记录下来，等 QApplication 就绪后再写入日志。
    bool dllSearchRestricted = true;
#ifdef Q_OS_WIN
    dllSearchRestricted = SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_APPLICATION_DIR |
                                                   LOAD_LIBRARY_SEARCH_SYSTEM32) != FALSE;
#endif

    // Velopack 生命周期钩子（安装/更新/卸载）必须在创建 Qt 应用前注册。
    // 普通启动时 Run() 立即返回；Velopack 派发的事件中可能直接退出进程。
#ifdef WITH_VELOPACK
    Velopack::VelopackApp::Build().Run();
#endif

    // 注意：必须在创建 QApplication 之前解析命令行。
    // QCoreApplication::arguments() 是静态函数，但没有实例时返回空列表。
    QStringList arguments;
    arguments.reserve(argc);
    for (int i = 0; i < argc; ++i)
        arguments << QString::fromLocal8Bit(argv[i]);

    if (arguments.contains(QStringLiteral("--help")) || arguments.contains(QStringLiteral("-h")) ||
        arguments.contains(QStringLiteral("/?"))) {
        printUsage();
        return 0;
    }
    if (arguments.contains(QStringLiteral("--version"))) {
        setupConsoleOutput();
        writeStdout(QStringLiteral("%1\n").arg(QStringLiteral(IMAGEPRO_VERSION)));
        return 0;
    }
    if (arguments.contains(QStringLiteral("--service-status"))) {
        printServiceStatus();
        return 0;
    }

    // ── 服务模式 ──────────────────────────────────────────────────
    // 服务运行在无桌面会话的环境里，QApplication（GUI）会启动失败，
    // 因此这里用 QCoreApplication。服务与桌面实例可以并存，
    // 所以不占用单实例锁。
    if (arguments.contains(QStringLiteral("--service"))) {
        QCoreApplication serviceApp(argc, argv);
        serviceApp.setApplicationName(QStringLiteral("影图"));
        serviceApp.setOrganizationName(QStringLiteral("yingtu"));

        yingtu::AutoProcessService service;
        yingtu::WindowsService::run(
            yingtu::WindowsService::defaultServiceName(), [&service]() { return service.run(); },
            [&service]() { service.requestStop(); });
        return 0;
    }

    // ── 注册 / 卸载服务（需要管理员权限）──────────────────────────
    // 清单声明为 asInvoker，因此这里主动以「runas」重新拉起自身触发 UAC。
    // --silent：供安装器调用时使用（安装器本身已有界面，不需要再弹一次）。
    const bool wantsInstall = arguments.contains(QStringLiteral("--install-service"));
    const bool wantsUninstall = arguments.contains(QStringLiteral("--uninstall-service"));
    const bool silent = arguments.contains(QStringLiteral("--silent"));
    if (wantsInstall || wantsUninstall) {
        if (!isAdministrator()) {
            const QStringList forwarded =
                wantsInstall
                    ? QStringList{QStringLiteral("--install-service"), QStringLiteral("--silent")}
                    : QStringList{QStringLiteral("--uninstall-service"),
                                  QStringLiteral("--silent")};
            if (relaunchElevated(forwarded))
                return 0;
            // 提权失败（用户取消 UAC）：继续以普通权限尝试并给出明确提示。
        }

        yingtu::ImageProApp app(argc, argv);
        app.initialize();

        const QString serviceName = yingtu::WindowsService::defaultServiceName();
        QString error;
        bool ok = false;
        if (wantsInstall) {
            ok = yingtu::WindowsService::install(
                serviceName, QStringLiteral("影图 自动处理服务"),
                QStringLiteral("监视指定目录，把新出现的图片按配置自动处理后输出。"), QString(),
                2 /* SERVICE_AUTO_START */, &error);
        } else {
            ok = yingtu::WindowsService::uninstall(serviceName, &error);
        }

        if (silent) {
            // 安装器调用：结果写日志即可，不要弹窗卡住安装流程。
            if (ok)
                yingtu::Logger::info(QStringLiteral("服务操作成功：%1").arg(serviceName),
                                     QStringLiteral("Service"));
            else
                yingtu::Logger::error(QStringLiteral("服务操作失败：%1").arg(error),
                                      QStringLiteral("Service"));
            return ok ? 0 : 1;
        }

        QMessageBox::information(
            nullptr, QStringLiteral("影图"),
            ok ? (wantsInstall
                      ? QStringLiteral("服务已注册：%1\n服务名：%2\n\n可在 services.msc 中管理，"
                                       "或使用「控制面板 → 程序和功能」卸载。")
                            .arg(QStringLiteral("影图 自动处理服务"), serviceName)
                      : QStringLiteral("服务已卸载。"))
               : error);
        return 0;
    }

    // 高分屏缩放：app.manifest 已声明 PerMonitorV2 感知，
    // 这里显式打开 Qt 的缩放开关，保证 125%/150% 缩放下界面不被系统位图拉伸。
    // 必须在 QApplication 构造前设置（Qt 在其构造时读取）。
    qputenv("QT_ENABLE_HIGHDPI_SCALING", "1");

    yingtu::ImageProApp app(argc, argv);

    // 启动路径包一层异常保护：此前任何构造期抛出的异常都会直接终止进程，
    // 用户看不到任何信息。
    try {
        app.initialize();

        if (!dllSearchRestricted) {
            yingtu::Logger::warning(QStringLiteral("SetDefaultDllDirectories failed; "
                                                   "DLL search path hardening is not active."),
                                    QStringLiteral("Startup"));
        }

        // 单实例：已有实例在运行时，把「显示窗口」的请求发给它并退出。
        // 用户再次双击图标时看到的是已有窗口被提到前台，而不是第二个窗口
        // 或一条「已在运行」的提示。
        // 守卫先于主窗口创建：若判定为重复实例，就不必构造主窗口。
        yingtu::SingleInstanceGuard guard;
        if (!guard.tryBecomePrimary())
            return 0;

        yingtu::MainWindow window;
        QObject::connect(&guard, &yingtu::SingleInstanceGuard::activationRequested, &window,
                         [&window]() { window.bringToFront(); });

        window.show();
        return app.exec();
    } catch (const std::exception& e) {
        QMessageBox::critical(
            nullptr, QStringLiteral("影图"),
            QStringLiteral("Startup failed: %1").arg(QString::fromUtf8(e.what())));
        return 1;
    } catch (...) {
        QMessageBox::critical(nullptr, QStringLiteral("影图"),
                              QStringLiteral("Startup failed with an unknown error."));
        return 1;
    }
}
