#pragma once

#include "config.hpp"

#include <functional>

namespace backup_service {

void create_backup(const configuration& config, const std::function<bool()>& should_stop);

} // namespace backup_service
