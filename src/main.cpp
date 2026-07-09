#include "app/ImageProApp.h"
#include "ui/MainWindow.h"

int main(int argc, char* argv[])
{
    yingtu::ImageProApp app(argc, argv);
    app.initialize();

    yingtu::MainWindow window;
    window.show();

    return app.exec();
}
