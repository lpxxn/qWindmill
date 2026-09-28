#pragma once

#include "models.h"

#include <QObject>
#include <QVector>

#include <atomic>
#include <optional>

// QObject 放入 QThread 后，run() 和它创建的 QTcpSocket 都在工作线程运行。
// 这样同步 TCP 等待不会卡住主线程的按钮、窗口重绘和取消操作。
class CollectorWorker final : public QObject {
    Q_OBJECT
public:
    CollectorWorker(CollectorConfig config, QueryPlan plan);

    // 该函数只修改原子变量，可以由 GUI 线程直接调用；不要在这里碰 QTcpSocket。
    void requestCancel() noexcept { cancelled_.store(true); }

public slots:
    void run();

signals:
    void logLine(const QString &text);
    void resultReady(const QList<DeviceReading> &readings);
    void failed(const QString &message);
    void finished();

private:
    std::optional<QVector<quint16>> readBlock(const QueryBlock &block, QString *error);
    bool sleepCancelable(int milliseconds);
    bool isCancelled() const noexcept { return cancelled_.load(); }

    CollectorConfig config_;
    QueryPlan plan_;
    std::atomic_bool cancelled_{false};
};
