#pragma once

#include "models.h"

#include <QByteArray>
#include <QVector>

// 12 字节厂家私有帧：12 34 56 78 90 06 01 03 起始地址(2) 数量(2)。
// 它借用 Modbus 03 功能码，但不是标准 Modbus TCP 的 MBAP 帧。
QByteArray makeReadRequest(quint16 start, quint16 quantity);

// 校验 9 字节头与数据区，并按大端序还原 16 位寄存器。
bool parseResponse(quint16 quantity, const QByteArray &header, const QByteArray &data,
                   QVector<quint16> *registers, QString *error);

// 一个地址块可以含两台设备；末批只含第 39 台。
QList<DeviceReading> decodeBlock(const QueryBlock &block, const QVector<quint16> &registers,
                                 const QDateTime &collectedAt, QString *error);
