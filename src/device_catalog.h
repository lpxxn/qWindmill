#pragma once

#include "models.h"

#include <optional>

const QList<DeviceDefinition> &deviceDefinitions();
std::optional<DeviceDefinition> deviceByAddress(int address);
std::optional<DeviceDefinition> deviceByStartAddress(quint16 startAddress);
