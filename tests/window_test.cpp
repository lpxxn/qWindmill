#include "main_window.h"

#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTreeWidget>
#include <QtEndian>
#include <QtTest>

class WindowTest final : public QObject {
    Q_OBJECT
private slots:
    void queryButtonShowsSelectedDeviceAndRequest();
};

void WindowTest::queryButtonShowsSelectedDeviceAndRequest()
{
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    QByteArray request;
    connect(&server, &QTcpServer::newConnection, this, [&] {
        auto *socket = server.nextPendingConnection();
        connect(socket, &QTcpSocket::readyRead, this, [socket, &request] {
            request += socket->readAll();
            if (request.size() != 12)
                return;
            QByteArray response = QByteArray::fromHex("1234567890C70103C4") + QByteArray(196, '\0');
            qToBigEndian(quint16(2300), reinterpret_cast<uchar *>(response.data() + 9));
            socket->write(response);
            socket->flush();
        });
    });

    qRegisterMetaType<QList<DeviceReading>>("QList<DeviceReading>");
    MainWindow window;
    QVERIFY2(!window.windowIcon().pixmap(32, 32).isNull(), "The application icon must load");
    window.show();
    auto *host = window.findChild<QLineEdit *>("hostEdit");
    auto *addresses = window.findChild<QLineEdit *>("addressesEdit");
    auto *port = window.findChild<QSpinBox *>("portSpin");
    auto *retries = window.findChild<QSpinBox *>("retriesSpin");
    auto *button = window.findChild<QPushButton *>("queryButton");
    auto *tree = window.findChild<QTreeWidget *>("resultsTree");
    auto *log = window.findChild<QPlainTextEdit *>("logEdit");
    QVERIFY(host && addresses && port && retries && button && tree && log);
    host->setText(QStringLiteral("127.0.0.1"));
    port->setValue(server.serverPort());
    retries->setValue(0);
    addresses->setText(QStringLiteral("1"));
    QTest::mouseClick(button, Qt::LeftButton);

    QTRY_COMPARE_WITH_TIMEOUT(tree->topLevelItemCount(), 1, 5000);
    QCOMPARE(request.toHex().toUpper(), QByteArray("123456789006010300000062"));
    QCOMPARE(tree->topLevelItem(0)->text(0), QStringLiteral("1"));
    QCOMPARE(tree->topLevelItem(0)->childCount(), 32);
    QCOMPARE(tree->topLevelItem(0)->child(0)->text(3), QStringLiteral("230"));
    QVERIFY(log->toPlainText().contains(QStringLiteral("请求地址：123456789006010300000062")));
    window.close();
}

QTEST_MAIN(WindowTest)
#include "window_test.moc"
