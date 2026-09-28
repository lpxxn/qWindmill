#include "collector_worker.h"

#include "protocol.h"

#include <QElapsedTimer>
#include <QHash>
#include <QTcpSocket>
#include <QThread>

#include <algorithm>

CollectorWorker::CollectorWorker(CollectorConfig config, QueryPlan plan)
    : config_(std::move(config)), plan_(std::move(plan))
{}

bool CollectorWorker::sleepCancelable(int milliseconds)
{
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < milliseconds) {
        if (isCancelled())
            return false;
        // 只在工作线程中短暂休眠；每 50 ms 检查一次取消标记。
        QThread::msleep(std::min(50, int(milliseconds - timer.elapsed())));
    }
    return !isCancelled();
}

std::optional<QVector<quint16>> CollectorWorker::readBlock(const QueryBlock &block, QString *error)
{
    const QByteArray request = makeReadRequest(block.start, block.quantity);
    if (request.isEmpty()) {
        *error = QStringLiteral("无效的寄存器读取数量：%1。").arg(block.quantity);
        return std::nullopt;
    }
    QTcpSocket socket; // 局部对象：本次请求结束即关闭连接，符合网关现场行为。
    socket.connectToHost(config_.host, config_.port);
    if (!socket.waitForConnected(config_.timeoutMs)) {
        *error = QStringLiteral("连接 %1:%2 失败：%3")
                     .arg(config_.host).arg(config_.port).arg(socket.errorString());
        return std::nullopt;
    }
    emit logLine(QStringLiteral("已连接：%1:%2")
                     .arg(socket.peerAddress().toString()).arg(socket.peerPort()));

    // Go 的 readOnce 使用一个绝对 I/O 截止时间覆盖写、响应头和数据区。
    // QElapsedTimer 使用单调时钟，系统时间调整不会延长这次请求的时限。
    QElapsedTimer ioTimer;
    ioTimer.start();
    auto remaining = [this, &ioTimer] { return std::max(0, config_.timeoutMs - int(ioTimer.elapsed())); };
    auto waitSlice = [&remaining] { return std::min(100, remaining()); };

    if (socket.write(request) != request.size()) {
        *error = QStringLiteral("请求报文写入缓冲区失败：%1").arg(socket.errorString());
        return std::nullopt;
    }
    while (socket.bytesToWrite() > 0) {
        if (isCancelled()) {
            *error = QStringLiteral("用户取消查询。");
            return std::nullopt;
        }
        if (remaining() == 0) {
            *error = QStringLiteral("发送请求超时。");
            return std::nullopt;
        }
        if (!socket.waitForBytesWritten(waitSlice()) && socket.state() != QAbstractSocket::ConnectedState) {
            *error = QStringLiteral("发送请求失败：%1").arg(socket.errorString());
            return std::nullopt;
        }
    }

    auto readExact = [&](qsizetype size, const QString &part) -> std::optional<QByteArray> {
        QByteArray bytes;
        bytes.reserve(size);
        while (bytes.size() < size) {
            if (isCancelled()) {
                *error = QStringLiteral("用户取消查询。");
                return std::nullopt;
            }
            bytes += socket.read(size - bytes.size());
            if (bytes.size() == size)
                break;
            if (remaining() == 0) {
                *error = QStringLiteral("读取%1超时：收到 %2/%3 字节。").arg(part).arg(bytes.size()).arg(size);
                return std::nullopt;
            }
            if (!socket.waitForReadyRead(waitSlice()) && socket.state() != QAbstractSocket::ConnectedState) {
                *error = QStringLiteral("读取%1失败：%2").arg(part, socket.errorString());
                return std::nullopt;
            }
        }
        return bytes;
    };

    // TCP 是字节流，响应可分多次到达。先精确读 9 字节固定头，
    // 再根据已知请求数量读取 2*N 字节，避免把半包解析成零值。
    const auto header = readExact(9, QStringLiteral("响应头"));
    if (!header)
        return std::nullopt;
    if (quint8(header->at(7)) & 0x80) {
        *error = QStringLiteral("设备异常响应：功能码 0x%1，异常码 0x%2。")
                     .arg(quint8(header->at(7)), 2, 16, QChar('0'))
                     .arg(quint8(header->at(8)), 2, 16, QChar('0'));
        return std::nullopt;
    }
    const auto data = readExact(block.quantity * 2, QStringLiteral("寄存器数据"));
    if (!data)
        return std::nullopt;
    QVector<quint16> registers;
    if (!parseResponse(block.quantity, *header, *data, &registers, error))
        return std::nullopt;
    return registers;
}

void CollectorWorker::run()
{
    QHash<int, DeviceReading> collected;
    for (int blockNumber = 0; blockNumber < plan_.blocks.size(); ++blockNumber) {
        const QueryBlock block = plan_.blocks.at(blockNumber);
        const QByteArray request = makeReadRequest(block.start, block.quantity);
        QString lastError;
        std::optional<QVector<quint16>> registers;
        for (int attempt = 1; attempt <= config_.retries + 1; ++attempt) {
            if (isCancelled()) {
                emit failed(QStringLiteral("用户取消查询。"));
                emit finished();
                return;
            }
            emit logLine(QStringLiteral("TCP 服务地址：%1:%2，批次起点：0x%3，数量：%4，尝试：%5/%6")
                             .arg(config_.host).arg(config_.port)
                             .arg(block.start, 4, 16, QChar('0'))
                             .arg(block.quantity).arg(attempt).arg(config_.retries + 1));
            emit logLine(QStringLiteral("请求地址：%1").arg(QString::fromLatin1(request.toHex().toUpper())));
            registers = readBlock(block, &lastError);
            if (registers)
                break;
            emit logLine(QStringLiteral("读取失败：%1").arg(lastError));
            if (attempt <= config_.retries && !sleepCancelable(config_.retryDelayMs))
                break;
        }
        if (!registers) {
            emit failed(isCancelled() ? QStringLiteral("用户取消查询。")
                                      : QStringLiteral("批次 0x%1 读取失败：%2")
                                            .arg(block.start, 4, 16, QChar('0')).arg(lastError));
            emit finished();
            return;
        }

        QString decodeError;
        const auto readings = decodeBlock(block, *registers, QDateTime::currentDateTime(), &decodeError);
        if (readings.isEmpty()) {
            emit failed(decodeError);
            emit finished();
            return;
        }
        for (const auto &reading : readings)
            collected.insert(reading.device.address, reading);
        emit logLine(QStringLiteral("批次 0x%1 完成，解析 %2 台设备。")
                         .arg(block.start, 4, 16, QChar('0')).arg(readings.size()));

        if (blockNumber + 1 < plan_.blocks.size() && !sleepCancelable(config_.requestGapMs)) {
            emit failed(QStringLiteral("用户取消查询。"));
            emit finished();
            return;
        }
    }

    // 批次可能顺带读到了未选中的相邻设备。只返回用户指定的 Address，
    // 并保持输入顺序；同一设备的重复输入已在 makeQueryPlan 中去重。
    QList<DeviceReading> selected;
    for (int address : plan_.requestedAddresses) {
        if (!collected.contains(address)) {
            emit failed(QStringLiteral("设备 Address %1 没有出现在采集结果中。").arg(address));
            emit finished();
            return;
        }
        selected.append(collected.value(address));
    }
    emit resultReady(selected);
    emit finished();
}
