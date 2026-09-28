#pragma once

#include <QDateTime>
#include <QList>
#include <QMetaType>
#include <QString>
#include <QVariant>

// 这些结构体只保存数据，不依赖窗口控件。协议解析可以单独测试，
// 界面也不必了解寄存器偏移或 TCP 帧的字节布局。
struct DeviceDefinition {
    int index = 0;           // 设备清单序号，从 1 开始；用于计算采集批次。
    int address = 0;         // 用户输入的 DeviceDefinition.Address，不是寄存器地址。
    QString name;
    quint16 startAddress = 0; // 该设备第一个 16 位寄存器的地址。
};

struct Measurement {
    QString key;             // 与 Go CLI 的 JSON 键一致，如 Ua、Psum。
    QString label;           // 界面上的中文说明。
    QString unit;
    QVariant value;          // Status 保存整数，其余工程量保存 double。
};

struct DeviceReading {
    DeviceDefinition device;
    QList<Measurement> measurements;
    QDateTime collectedAt;
};

struct QueryBlock {
    quint16 start = 0;
    quint16 quantity = 0;
};

struct QueryPlan {
    QList<int> requestedAddresses; // 去重后仍保留用户输入顺序。
    QList<QueryBlock> blocks;      // 同一批只发一条 TCP 请求。
};

struct CollectorConfig {
    QString host = QStringLiteral("wg3.joyconn.top");
    quint16 port = 20029;
    int timeoutMs = 5000;
    int requestGapMs = 3200;
    int retries = 2;
    int retryDelayMs = 1000;
};

// queued connection 会在 GUI 线程收到一份结果副本，不会跨线程直接访问控件。
Q_DECLARE_METATYPE(QList<DeviceReading>)
