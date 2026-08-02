#include "app/ImageProApp.h"
#include "ui/MainWindow.h"

#ifdef Q_OS_WIN
#include <windows.h>
#endif

#ifdef WITH_VELOPACK
#include <Velopack.hpp>
#endif

int main(int argc, char* argv[])
{
#ifdef Q_OS_WIN
    // 限制 DLL 搜索路径为程序目录与 System32，避免用户 PATH 中同名但不兼容的 DLL 导致加载冲突
    SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_APPLICATION_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
#endif

    // Velopack 生命周期钩子（安装/更新/卸载）必须在创建 Qt 应用前注册。
    // 普通启动时 Run() 立即返回；Velopack 派发的事件中可能直接退出进程。
#ifdef WITH_VELOPACK
    Velopack::VelopackApp::Build().Run();
#endif

    yingtu::ImageProApp app(argc, argv);
    app.initialize();

    yingtu::MainWindow window;
    window.show();

    return app.exec();
}
