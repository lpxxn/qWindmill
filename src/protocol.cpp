#include "protocol.h"

#include "device_catalog.h"

#include <QtEndian>

#include <bit>
#include <cmath>
#include <cstdint>

namespace {
constexpr int registersPerDevice = 49;
constexpr int responseHeaderSize = 9;

quint8 byteAt(const QByteArray &bytes, qsizetype index)
{
    // QByteArray::operator[] 返回 char。先转为 quint8，避免 0x80 以上
    // 的字节被当成负数，影响协议长度和功能码判断。
    return static_cast<quint8>(bytes.at(index));
}

double scaled(double raw, double factor)
{
    // 与 Go 的 applyScale 一致：按倍率的小数位再次四舍五入。
    // 例如 3872*0.1 得到 387.2，避免浮点表示噪声进入界面。
    const double decimalFactor = std::round(1.0 / factor);
    return std::round(raw * factor * decimalFactor) / decimalFactor;
}

double u16(const QVector<quint16> &r, int offset, double factor)
{
    return scaled(r.at(offset), factor);
}

double i16(const QVector<quint16> &r, int offset, double factor)
{
    // 16 位补码解释温度；0xFFFF 即 -1，而不是 65535。
    return scaled(std::bit_cast<qint16>(r.at(offset)), factor);
}

double i32(const QVector<quint16> &r, int offset, double factor)
{
    // Excel 协议使用“高寄存器在前、低寄存器在后”。
    // bit_cast 保留原始 32 位位型，负的功率等数据按补码解读。
    const quint32 raw = (quint32(r.at(offset)) << 16) | quint32(r.at(offset + 1));
    return scaled(std::bit_cast<qint32>(raw), factor);
}

DeviceReading decodeDevice(const DeviceDefinition &definition, const QVector<quint16> &r,
                           const QDateTime &collectedAt)
{
    DeviceReading result{definition, {}, collectedAt};
    auto add = [&result](const QString &key, const QString &label, const QString &unit, double value) {
        result.measurements.append({key, label, unit, value});
    };

    // 偏移、数据类型、倍率逐项对应 Go 的 collector.decodeDevice。
    // u16/i16/i32 负责类型解码；此处只声明每个业务字段所在偏移。
    add(QStringLiteral("Ua"), QStringLiteral("A 相电压"), QStringLiteral("V"), u16(r, 0, .1));
    add(QStringLiteral("Ub"), QStringLiteral("B 相电压"), QStringLiteral("V"), u16(r, 1, .1));
    add(QStringLiteral("Uc"), QStringLiteral("C 相电压"), QStringLiteral("V"), u16(r, 2, .1));
    add(QStringLiteral("Uab"), QStringLiteral("AB 线电压"), QStringLiteral("V"), u16(r, 3, .1));
    add(QStringLiteral("Ubc"), QStringLiteral("BC 线电压"), QStringLiteral("V"), u16(r, 4, .1));
    add(QStringLiteral("Uca"), QStringLiteral("CA 线电压"), QStringLiteral("V"), u16(r, 5, .1));

    add(QStringLiteral("Ia"), QStringLiteral("A 相电流"), QStringLiteral("A"), i32(r, 6, .01));
    add(QStringLiteral("Ib"), QStringLiteral("B 相电流"), QStringLiteral("A"), i32(r, 8, .01));
    add(QStringLiteral("Ic"), QStringLiteral("C 相电流"), QStringLiteral("A"), i32(r, 10, .01));

    add(QStringLiteral("Psum"), QStringLiteral("总有功功率"), QStringLiteral("kW"), i32(r, 12, .01));
    add(QStringLiteral("Pa"), QStringLiteral("A 相有功功率"), QStringLiteral("kW"), i32(r, 14, .01));
    add(QStringLiteral("Pb"), QStringLiteral("B 相有功功率"), QStringLiteral("kW"), i32(r, 16, .01));
    add(QStringLiteral("Pc"), QStringLiteral("C 相有功功率"), QStringLiteral("kW"), i32(r, 18, .01));

    add(QStringLiteral("Qsum"), QStringLiteral("总无功功率"), QStringLiteral("kvar"), i32(r, 20, .01));
    add(QStringLiteral("Qa"), QStringLiteral("A 相无功功率"), QStringLiteral("kvar"), i32(r, 22, .01));
    add(QStringLiteral("Qb"), QStringLiteral("B 相无功功率"), QStringLiteral("kvar"), i32(r, 24, .01));
    add(QStringLiteral("Qc"), QStringLiteral("C 相无功功率"), QStringLiteral("kvar"), i32(r, 26, .01));

    add(QStringLiteral("Ssum"), QStringLiteral("总视在功率"), QStringLiteral("kVA"), u16(r, 28, .1));
    add(QStringLiteral("Sa"), QStringLiteral("A 相视在功率"), QStringLiteral("kVA"), u16(r, 29, .1));
    add(QStringLiteral("Sb"), QStringLiteral("B 相视在功率"), QStringLiteral("kVA"), u16(r, 30, .1));
    add(QStringLiteral("Sc"), QStringLiteral("C 相视在功率"), QStringLiteral("kVA"), u16(r, 31, .1));

    add(QStringLiteral("PF"), QStringLiteral("总功率因数"), {}, i32(r, 32, .001));
    add(QStringLiteral("PFa"), QStringLiteral("A 相功率因数"), {}, i32(r, 34, .001));
    add(QStringLiteral("PFB"), QStringLiteral("B 相功率因数"), {}, i32(r, 36, .001));
    add(QStringLiteral("PFC"), QStringLiteral("C 相功率因数"), {}, i32(r, 38, .001));

    add(QStringLiteral("F"), QStringLiteral("频率"), QStringLiteral("Hz"), u16(r, 40, .1));
    result.measurements.append({QStringLiteral("Status"), QStringLiteral("开关状态原始值"), {}, r.at(41)});
    add(QStringLiteral("Eptotal"), QStringLiteral("总电能"), QStringLiteral("kWh"), i32(r, 42, .01));
    add(QStringLiteral("fKwhRImp"), QStringLiteral("正向无功总电能"), QStringLiteral("kvarh"), i32(r, 44, .01));
    add(QStringLiteral("fTempPha"), QStringLiteral("A 相温度"), QStringLiteral("℃"), i16(r, 46, .1));
    add(QStringLiteral("fTempPhb"), QStringLiteral("B 相温度"), QStringLiteral("℃"), i16(r, 47, .1));
    add(QStringLiteral("fTempPhc"), QStringLiteral("C 相温度"), QStringLiteral("℃"), i16(r, 48, .1));
    return result;
}
} // namespace

QByteArray makeReadRequest(quint16 start, quint16 quantity)
{
    if (quantity == 0 || quantity > 126)
        return {}; // 1 字节帧长度为 3+2*N，N 最大只能是 126。
    QByteArray request(12, '\0');
    request[0] = char(0x12); request[1] = char(0x34); request[2] = char(0x56);
    request[3] = char(0x78); request[4] = char(0x90);
    request[5] = char(0x06); // 请求中此字段恒为后续 6 字节的长度。
    request[6] = char(0x01); // 现场单元号。
    request[7] = char(0x03); // 读保持寄存器功能码。
    qToBigEndian(start, reinterpret_cast<uchar *>(request.data() + 8));
    qToBigEndian(quantity, reinterpret_cast<uchar *>(request.data() + 10));
    return request;
}

bool parseResponse(quint16 quantity, const QByteArray &header, const QByteArray &data,
                   QVector<quint16> *registers, QString *error)
{
    auto fail = [error](const QString &message) {
        if (error)
            *error = message;
        return false;
    };
    if (header.size() != responseHeaderSize)
        return fail(QStringLiteral("响应头长度不是 9 字节。"));
    if (header.left(5) != QByteArray::fromHex("1234567890"))
        return fail(QStringLiteral("响应帧前缀不匹配。"));
    if (byteAt(header, 6) != 1)
        return fail(QStringLiteral("响应单元号不是 01。"));
    if (byteAt(header, 7) & 0x80)
        return fail(QStringLiteral("设备异常响应：功能码 0x%1，异常码 0x%2。")
                    .arg(byteAt(header, 7), 2, 16, QChar('0'))
                    .arg(byteAt(header, 8), 2, 16, QChar('0')));
    if (byteAt(header, 7) != 3)
        return fail(QStringLiteral("响应功能码不是 03。"));
    const int expectedBytes = int(quantity) * 2;
    if (byteAt(header, 8) != expectedBytes || byteAt(header, 5) != expectedBytes + 3)
        return fail(QStringLiteral("响应的字节数或帧长度与请求数量 %1 不一致。").arg(quantity));
    if (data.size() != expectedBytes)
        return fail(QStringLiteral("响应数据不完整：收到 %1 字节，应有 %2 字节。")
                    .arg(data.size()).arg(expectedBytes));

    registers->clear();
    registers->reserve(quantity);
    for (int i = 0; i < quantity; ++i) {
        registers->append(qFromBigEndian<quint16>(
            reinterpret_cast<const uchar *>(data.constData() + i * 2)));
    }
    return true;
}

QList<DeviceReading> decodeBlock(const QueryBlock &block, const QVector<quint16> &registers,
                                 const QDateTime &collectedAt, QString *error)
{
    if (registers.size() != block.quantity || block.start % registersPerDevice != 0
        || registers.size() % registersPerDevice != 0) {
        if (error)
            *error = QStringLiteral("批次寄存器数量或起始地址没有按 49 对齐。");
        return {};
    }
    QList<DeviceReading> readings;
    for (int offset = 0; offset < registers.size(); offset += registersPerDevice) {
        const auto start = quint16(block.start + offset);
        const auto definition = deviceByStartAddress(start);
        if (!definition) {
            if (error)
                *error = QStringLiteral("起始寄存器 0x%1 没有对应设备。")
                             .arg(start, 4, 16, QChar('0'));
            return {};
        }
        readings.append(decodeDevice(*definition, registers.mid(offset, registersPerDevice), collectedAt));
    }
    return readings;
}
