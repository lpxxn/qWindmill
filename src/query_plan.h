#pragma once

#include "models.h"

#include <optional>

// 从用户输入生成真正需要发送的地址块。出错时返回 nullopt，并写入中文原因。
std::optional<QueryPlan> makeQueryPlan(const QString &input, QString *error);
