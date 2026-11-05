#pragma once

#include <cstdint>

namespace nsync::net {

int run_kcp_server(std::uint16_t port, int duration_ms, int delay_ms, int loss_percent);
int run_kcp_client(const char* host, std::uint16_t port, int duration_ms, int delay_ms, int loss_percent);

} // namespace nsync::net
