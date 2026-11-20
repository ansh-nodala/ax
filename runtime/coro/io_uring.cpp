#include "runtime/coro/io_uring.hpp"
#include <unistd.h>
#include <sys/syscall.h>
#include <sys/epoll.h>
#include <linux/io_uring.h>
#include <cerrno>
namespace ax {
#ifndef __NR_io_uring_setup
#define __NR_io_uring_setup 425
#endif
#ifndef __NR_io_uring_enter
#define __NR_io_uring_enter 426
#endif
#ifndef __NR_io_uring_register
#define __NR_io_uring_register 427
#endif
int ax_io_uring_setup(unsigned entries, void* params){ long r=syscall(__NR_io_uring_setup, entries, params); return (int)r; }
int ax_io_uring_enter(int fd, unsigned to_submit, unsigned min_complete, unsigned flags){ long r=syscall(__NR_io_uring_enter, fd, to_submit, min_complete, flags, nullptr, 0); return (int)r; }
bool ax_io_uring_available(){ struct io_uring_params params{}; int fd=ax_io_uring_setup(8, &params); if(fd>=0){ close(fd); return true; } return false; }
int ax_epoll_fallback_wait(int fd, int timeout_ms){ int ep=epoll_create1(0); if(ep<0) return -1; struct epoll_event ev{}; ev.events=EPOLLIN; ev.data.fd=fd; epoll_ctl(ep, EPOLL_CTL_ADD, fd, &ev); struct epoll_event out[1]; int n=epoll_wait(ep, out, 1, timeout_ms); close(ep); return n; }
} // namespace ax
