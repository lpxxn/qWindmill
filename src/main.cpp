#include "main_window.h"

#include <QApplication>
#include <QIcon>

int main(int argc, char *argv[])
{
    QApplication application(argc, argv);
    // 设置应用图标：窗口标题栏、任务栏和 Alt+Tab 都使用同一枚矢量图。
    application.setWindowIcon(QIcon(QStringLiteral(":/icons/app.svg")));
    qRegisterMetaType<QList<DeviceReading>>("QList<DeviceReading>");
    MainWindow window;
    window.show();
    return application.exec();
}
