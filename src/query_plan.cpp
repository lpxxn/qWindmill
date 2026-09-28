#include "query_plan.h"

#include "device_catalog.h"

#include <QRegularExpression>
#include <QSet>

#include <algorithm>

std::optional<QueryPlan> makeQueryPlan(const QString &input, QString *error)
{
    const auto tokens = input.split(QRegularExpression(QStringLiteral("[,，\\s]+")), Qt::SkipEmptyParts);
    if (tokens.isEmpty()) {
        if (error)
            *error = QStringLiteral("请输入至少一个设备 Address，例如 1 或 1,2,39。");
        return std::nullopt;
    }

    QueryPlan plan;
    QSet<int> seenAddresses;
    QSet<int> seenBlocks;
    const auto &catalog = deviceDefinitions();
    const int totalRegisters = int(catalog.last().startAddress) + 49;

    for (const auto &token : tokens) {
        bool numeric = false;
        const int address = token.toInt(&numeric, 10);
        const auto definition = numeric ? deviceByAddress(address) : std::nullopt;
        if (!definition) {
            if (error)
                *error = QStringLiteral("无效的设备 Address：%1；请输入设备清单中的十进制地址。").arg(token);
            return std::nullopt; // 全部先校验，避免部分地址已读取后才发现错误。
        }
        if (seenAddresses.contains(address))
            continue;
        seenAddresses.insert(address);
        plan.requestedAddresses.append(address);

        // DeviceDefinition.Index 从 1 开始，两个设备共享 98 个寄存器。
        // 整数除法使 1/2 -> 批次 0，3/4 -> 批次 1，5/6 -> 批次 2。
        // Address 不能直接用于寄存器计算，必须先从设备清单拿到 Index。
        const int blockIndex = (definition->index - 1) / 2;
        if (seenBlocks.contains(blockIndex))
            continue; // 输入 1 和 2 时只发送 0x0000/98 一条请求。
        seenBlocks.insert(blockIndex);

        const int start = blockIndex * 98; // 0x0000、0x0062、0x00C4……
        const int quantity = std::min(98, totalRegisters - start);
        // 39 台设备共 1911 个寄存器；批次 19 从 1862 (0x0746) 开始，
        // 只剩 49 个寄存器，所以末批沿用 Go CLI 的 49 寄存器请求。
        plan.blocks.append({quint16(start), quint16(quantity)});
    }
    return plan;
}
