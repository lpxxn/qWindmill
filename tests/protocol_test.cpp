#include "collector_worker.h"
#include "device_catalog.h"
#include "protocol.h"
#include "query_plan.h"

#include <QtEndian>
#include <QtTest>
#include <QTcpServer>
#include <QTcpSocket>
#include <QThread>

class ProtocolTest final : public QObject {
    Q_OBJECT
private slots:
    void deviceCatalogMatchesGo();
    void plansOnlyRequiredBatches();
    void buildsExactRequests();
    void decodesCompleteMeasurements();
    void rejectsInvalidResponse();
    void workerReadsOneSelectedDeviceFromPair();
};

void ProtocolTest::deviceCatalogMatchesGo()
{
    const auto &devices = deviceDefinitions();
    QCOMPARE(devices.size(), 39);
    for (int i = 0; i < devices.size(); ++i) {
        QCOMPARE(devices.at(i).index, i + 1);
        QCOMPARE(devices.at(i).address, i + 1);
        QCOMPARE(devices.at(i).startAddress, quint16(i * 49));
    }
}

void ProtocolTest::plansOnlyRequiredBatches()
{
    QString error;
    const auto plan = makeQueryPlan(QStringLiteral("4, 1 3,1,39"), &error);
    QVERIFY2(plan.has_value(), qPrintable(error));
    QCOMPARE(plan->requestedAddresses, (QList<int>{4, 1, 3, 39}));
    QCOMPARE(plan->blocks.size(), 3);
    QCOMPARE(plan->blocks.at(0).start, quint16(98));
    QCOMPARE(plan->blocks.at(0).quantity, quint16(98));
    QCOMPARE(plan->blocks.at(1).start, quint16(0));
    QCOMPARE(plan->blocks.at(1).quantity, quint16(98));
    QCOMPARE(plan->blocks.at(2).start, quint16(1862));
    QCOMPARE(plan->blocks.at(2).quantity, quint16(49));
    QVERIFY(!makeQueryPlan(QStringLiteral("1 40"), &error));
    QVERIFY(error.contains(QStringLiteral("40")));
}

void ProtocolTest::buildsExactRequests()
{
    QCOMPARE(makeReadRequest(0, 98).toHex().toUpper(), QByteArray("123456789006010300000062"));
    QCOMPARE(makeReadRequest(98, 98).toHex().toUpper(), QByteArray("123456789006010300620062"));
    QCOMPARE(makeReadRequest(196, 98).toHex().toUpper(), QByteArray("123456789006010300C40062"));
    QCOMPARE(makeReadRequest(1862, 49).toHex().toUpper(), QByteArray("123456789006010307460031"));
}

void ProtocolTest::decodesCompleteMeasurements()
{
    QByteArray data(196, '\0');
    auto put = [&data](int registerIndex, quint16 value) {
        qToBigEndian(value, reinterpret_cast<uchar *>(data.data() + registerIndex * 2));
    };
    put(0, 2259);       // Address 1 的 Ua = 225.9 V。
    put(41, 1);         // Status 保留原始整数。
    put(46, 0xFFF6);   // 温度 -10 * 0.1 = -1.0 ℃。
    put(49, 2270);     // Address 2 的 Ua = 227.0 V。

    const QByteArray header = QByteArray::fromHex("1234567890C70103C4");
    QVector<quint16> registers;
    QString error;
    QVERIFY2(parseResponse(98, header, data, &registers, &error), qPrintable(error));
    const auto readings = decodeBlock({0, 98}, registers, QDateTime::currentDateTime(), &error);
    QCOMPARE(readings.size(), 2);
    QCOMPARE(readings.at(0).device.address, 1);
    QCOMPARE(readings.at(1).device.address, 2);
    QCOMPARE(readings.at(0).measurements.size(), 32);
    QCOMPARE(readings.at(0).measurements.at(0).value.toDouble(), 225.9);
    QCOMPARE(readings.at(0).measurements.at(26).key, QStringLiteral("Status"));
    QCOMPARE(readings.at(0).measurements.at(26).value.toInt(), 1);
    QCOMPARE(readings.at(0).measurements.at(29).value.toDouble(), -1.0);
    QCOMPARE(readings.at(1).measurements.at(0).value.toDouble(), 227.0);
}

void ProtocolTest::rejectsInvalidResponse()
{
    QVector<quint16> registers;
    QString error;
    QVERIFY(!parseResponse(98, QByteArray::fromHex("123456789065010362"),
                           QByteArray(98, '\0'), &registers, &error));
    QVERIFY(!error.isEmpty());
}

void ProtocolTest::workerReadsOneSelectedDeviceFromPair()
{
    // 本地假网关验证真正的 TCP 路径：只输入 Address 1，网络请求仍须
    // 读取 Address 1/2 共 98 个寄存器，但结果只返回 Address 1。
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    QByteArray receivedRequest;
    connect(&server, &QTcpServer::newConnection, this, [&] {
        auto *socket = server.nextPendingConnection();
        connect(socket, &QTcpSocket::readyRead, this, [socket, &receivedRequest] {
            receivedRequest += socket->readAll();
            if (receivedRequest.size() != 12)
                return;
            QByteArray response = QByteArray::fromHex("1234567890C70103C4") + QByteArray(196, '\0');
            qToBigEndian(quint16(2260), reinterpret_cast<uchar *>(response.data() + 9));
            socket->write(response);
            socket->flush();
        });
    });

    QString error;
    const auto plan = makeQueryPlan(QStringLiteral("1"), &error);
    QVERIFY2(plan.has_value(), qPrintable(error));
    CollectorConfig config;
    config.host = QStringLiteral("127.0.0.1");
    config.port = server.serverPort();
    config.timeoutMs = 1000;
    config.retries = 0;
    config.requestGapMs = 0;

    qRegisterMetaType<QList<DeviceReading>>("QList<DeviceReading>");
    QThread thread;
    auto *worker = new CollectorWorker(config, *plan);
    worker->moveToThread(&thread);
    QSignalSpy results(worker, &CollectorWorker::resultReady);
    QSignalSpy failures(worker, &CollectorWorker::failed);
    QSignalSpy finished(worker, &CollectorWorker::finished);
    connect(&thread, &QThread::started, worker, &CollectorWorker::run);
    connect(worker, &CollectorWorker::finished, &thread, &QThread::quit, Qt::DirectConnection);
    connect(worker, &CollectorWorker::finished, worker, &QObject::deleteLater);
    thread.start();
    const bool completed = finished.wait(5000);
    worker = nullptr; // finished 后对象由 deleteLater 负责，不再解引用。
    thread.quit();
    QVERIFY(thread.wait(5000));

    QVERIFY(completed);
    QCOMPARE(receivedRequest.toHex().toUpper(), QByteArray("123456789006010300000062"));
    QCOMPARE(failures.size(), 0);
    QCOMPARE(results.size(), 1);
    const auto readings = qvariant_cast<QList<DeviceReading>>(results.first().first());
    QCOMPARE(readings.size(), 1);
    QCOMPARE(readings.first().device.address, 1);
    QCOMPARE(readings.first().measurements.first().value.toDouble(), 226.0);
}

QTEST_GUILESS_MAIN(ProtocolTest)
#include "protocol_test.moc"
