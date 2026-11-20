#pragma once
#include <cstddef>
namespace ax {
int ax_io_uring_setup(unsigned entries, void* params);
int ax_io_uring_enter(int fd, unsigned to_submit, unsigned min_complete, unsigned flags);
bool ax_io_uring_available();
int ax_epoll_fallback_wait(int fd, int timeout_ms);
} // namespace ax
