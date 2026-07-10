#include "app/ImageProApp.h"
#include "ui/MainWindow.h"

#ifdef Q_OS_WIN
#include <windows.h>
#endif

int main(int argc, char* argv[])
{
#ifdef Q_OS_WIN
    // 限制 DLL 搜索路径为程序目录与 System32，避免用户 PATH 中同名但不兼容的 DLL 导致加载冲突
    SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_APPLICATION_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
#endif

    yingtu::ImageProApp app(argc, argv);
    app.initialize();

    yingtu::MainWindow window;
    window.show();

    return app.exec();
}
