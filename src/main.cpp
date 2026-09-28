#include "main_window.h"

#include <QApplication>

int main(int argc, char *argv[])
{
    QApplication application(argc, argv);
    qRegisterMetaType<QList<DeviceReading>>("QList<DeviceReading>");
    MainWindow window;
    window.show();
    return application.exec();
}
