#include "device_catalog.h"

const QList<DeviceDefinition> &deviceDefinitions()
{
    // 与 Go 的 apps/windmill/internal/collector/devices.go 保持相同顺序。
    // 名称可以变化，但 index、address 和 startAddress 的协议映射不能排序。
    static const QList<DeviceDefinition> devices{
        {1, 1, QStringLiteral("(AA30)[低压进线] 403-2"), 0},
        {2, 2, QStringLiteral("(AA30-1)[出线柜] 437"), 49},
        {3, 3, QStringLiteral("(AA33)[出线柜] 431-1"), 98},
        {4, 4, QStringLiteral("(AA33)[出线柜] 431-2"), 147},
        {5, 5, QStringLiteral("(AA33)[出线柜] 431-3"), 196},
        {6, 6, QStringLiteral("(AA33)[出线柜] 431-4"), 245},
        {7, 7, QStringLiteral("(AA33)[出线柜] 431-5"), 294},
        {8, 8, QStringLiteral("(AA34)[出线柜] 432-1"), 343},
        {9, 9, QStringLiteral("(AA34)[出线柜] 432-2"), 392},
        {10, 10, QStringLiteral("(AA34)[出线柜] 432-3"), 441},
        {11, 11, QStringLiteral("(AA34)[出线柜] 432-4"), 490},
        {12, 12, QStringLiteral("(AA34)[出线柜] 432-5"), 539},
        {13, 13, QStringLiteral("(AA34)[出线柜] 432-6"), 588},
        {14, 14, QStringLiteral("(AA35)[出线柜] 433-1"), 637},
        {15, 15, QStringLiteral("(AA35)[出线柜] 433-2"), 686},
        {16, 16, QStringLiteral("(AA35)[出线柜] 433-3"), 735},
        {17, 17, QStringLiteral("(AA35)[出线柜] 433-4"), 784},
        {18, 18, QStringLiteral("(AA35)[出线柜] 433-5"), 833},
        {19, 19, QStringLiteral("(AA35)[出线柜] 433-6"), 882},
        {20, 20, QStringLiteral("(AA35)[出线柜] 433-7"), 931},
        {21, 21, QStringLiteral("(AA36)[出线柜] 434-1"), 980},
        {22, 22, QStringLiteral("(AA36)[出线柜] 434-2"), 1029},
        {23, 23, QStringLiteral("(AA36)[出线柜] 434-3"), 1078},
        {24, 24, QStringLiteral("(AA36)[出线柜] 434-4"), 1127},
        {25, 25, QStringLiteral("(AA36)[出线柜] 434-5"), 1176},
        {26, 26, QStringLiteral("(AA37)[出线柜] 435-1"), 1225},
        {27, 27, QStringLiteral("(AA37)[出线柜] 435-2"), 1274},
        {28, 28, QStringLiteral("(AA37)[出线柜] 435-3"), 1323},
        {29, 29, QStringLiteral("(AA37)[出线柜] 435-4"), 1372},
        {30, 30, QStringLiteral("(AA37)[出线柜] 435-5"), 1421},
        {31, 31, QStringLiteral("(AA37)[出线柜] 435-6"), 1470},
        {32, 32, QStringLiteral("(AA37)[出线柜] 435-7"), 1519},
        {33, 33, QStringLiteral("(AA37)[出线柜] 435-8"), 1568},
        {34, 34, QStringLiteral("(AA37)[出线柜] 435-9"), 1617},
        {35, 35, QStringLiteral("(AA9)[出线柜] 436-1"), 1666},
        {36, 36, QStringLiteral("(AA9)[出线柜] 436-2"), 1715},
        {37, 37, QStringLiteral("(AA9)[出线柜] 436-3"), 1764},
        {38, 38, QStringLiteral("(AA9)[出线柜] 436-4"), 1813},
        {39, 39, QStringLiteral("(AA9)[出线柜] 436-5"), 1862},
    };
    return devices;
}

std::optional<DeviceDefinition> deviceByAddress(int address)
{
    for (const auto &device : deviceDefinitions()) {
        if (device.address == address)
            return device;
    }
    return std::nullopt;
}

std::optional<DeviceDefinition> deviceByStartAddress(quint16 startAddress)
{
    for (const auto &device : deviceDefinitions()) {
        if (device.startAddress == startAddress)
            return device;
    }
    return std::nullopt;
}
