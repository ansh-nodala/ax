#include "runtime/dist/dist.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cerrno>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

namespace ax {
namespace {

constexpr std::uint32_t wire_magic = 0x41585231u;
constexpr std::size_t transfer_block_values = 1024;
constexpr int socket_timeout_seconds = 5;

std::array<std::uint8_t, 4> encode_u32(std::uint32_t value) {
  return {static_cast<std::uint8_t>(value), static_cast<std::uint8_t>(value >> 8),
          static_cast<std::uint8_t>(value >> 16), static_cast<std::uint8_t>(value >> 24)};
}

std::uint32_t decode_u32(const std::array<std::uint8_t, 4>& bytes) {
  return static_cast<std::uint32_t>(bytes[0]) |
         (static_cast<std::uint32_t>(bytes[1]) << 8) |
         (static_cast<std::uint32_t>(bytes[2]) << 16) |
         (static_cast<std::uint32_t>(bytes[3]) << 24);
}

std::array<std::uint8_t, 8> encode_u64(std::uint64_t value) {
  std::array<std::uint8_t, 8> bytes{};
  for (unsigned i = 0; i < 8; ++i) bytes[i] = static_cast<std::uint8_t>(value >> (i * 8));
  return bytes;
}

std::uint64_t decode_u64(const std::array<std::uint8_t, 8>& bytes) {
  std::uint64_t value = 0;
  for (unsigned i = 0; i < 8; ++i) value |= static_cast<std::uint64_t>(bytes[i]) << (i * 8);
  return value;
}

bool send_all(int socket, const void* source, std::size_t size) {
  const auto* bytes = static_cast<const std::uint8_t*>(source);
  while (size > 0) {
    const ssize_t sent = ::send(socket, bytes, size, MSG_NOSIGNAL);
    if (sent < 0 && errno == EINTR) continue;
    if (sent <= 0) return false;
    bytes += sent;
    size -= static_cast<std::size_t>(sent);
  }
  return true;
}

bool receive_all(int socket, void* destination, std::size_t size) {
  auto* bytes = static_cast<std::uint8_t*>(destination);
  while (size > 0) {
    const ssize_t received = ::recv(socket, bytes, size, 0);
    if (received < 0 && errno == EINTR) continue;
    if (received <= 0) return false;
    bytes += received;
    size -= static_cast<std::size_t>(received);
  }
  return true;
}

void set_socket_timeout(int socket) {
  const timeval timeout{socket_timeout_seconds, 0};
  ::setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
  ::setsockopt(socket, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
}

bool send_vector(int socket, const std::vector<double>& values) {
  std::array<std::uint8_t, transfer_block_values * sizeof(double)> encoded{};
  for (std::size_t offset = 0; offset < values.size(); offset += transfer_block_values) {
    const std::size_t count = std::min(transfer_block_values, values.size() - offset);
    for (std::size_t i = 0; i < count; ++i) {
      const std::uint64_t bits = std::bit_cast<std::uint64_t>(values[offset + i]);
      for (unsigned byte = 0; byte < 8; ++byte) {
        encoded[i * 8 + byte] = static_cast<std::uint8_t>(bits >> (byte * 8));
      }
    }
    if (!send_all(socket, encoded.data(), count * sizeof(double))) return false;
  }
  return true;
}

bool receive_vector(int socket, std::vector<double>& values) {
  std::array<std::uint8_t, transfer_block_values * sizeof(double)> encoded{};
  for (std::size_t offset = 0; offset < values.size(); offset += transfer_block_values) {
    const std::size_t count = std::min(transfer_block_values, values.size() - offset);
    if (!receive_all(socket, encoded.data(), count * sizeof(double))) return false;
    for (std::size_t i = 0; i < count; ++i) {
      std::uint64_t bits = 0;
      for (unsigned byte = 0; byte < 8; ++byte) {
        bits |= static_cast<std::uint64_t>(encoded[i * 8 + byte]) << (byte * 8);
      }
      values[offset + i] = std::bit_cast<double>(bits);
    }
  }
  return true;
}

int listen_socket(int port) {
  const int listener = ::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
  if (listener < 0) return -1;
  int reuse = 1;
  ::setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_ANY);
  address.sin_port = htons(static_cast<std::uint16_t>(port));
  if (::bind(listener, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0 ||
      ::listen(listener, 64) != 0) {
    ::close(listener);
    return -1;
  }
  return listener;
}

int connect_socket(const std::string& host, int port) {
  addrinfo hints{};
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  addrinfo* results = nullptr;
  if (::getaddrinfo(host.c_str(), nullptr, &hints, &results) != 0) return -1;
  int connected = -1;
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
  do {
    for (addrinfo* item = results; item != nullptr && connected < 0; item = item->ai_next) {
      const int socket = ::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
      if (socket < 0) continue;
      sockaddr_in address = *reinterpret_cast<sockaddr_in*>(item->ai_addr);
      address.sin_port = htons(static_cast<std::uint16_t>(port));
      if (::connect(socket, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0) {
        connected = socket;
        set_socket_timeout(connected);
      } else {
        ::close(socket);
      }
    }
    if (connected < 0) std::this_thread::sleep_for(std::chrono::milliseconds(25));
  } while (connected < 0 && std::chrono::steady_clock::now() < deadline);
  ::freeaddrinfo(results);
  return connected;
}

bool send_header(int socket, std::uint32_t rank, std::size_t count) {
  const auto magic = encode_u32(wire_magic);
  const auto encoded_rank = encode_u32(rank);
  const auto encoded_count = encode_u64(static_cast<std::uint64_t>(count));
  return send_all(socket, magic.data(), magic.size()) &&
         send_all(socket, encoded_rank.data(), encoded_rank.size()) &&
         send_all(socket, encoded_count.data(), encoded_count.size());
}

bool receive_header(int socket, std::uint32_t& rank, std::uint64_t& count) {
  std::array<std::uint8_t, 4> magic{};
  std::array<std::uint8_t, 4> encoded_rank{};
  std::array<std::uint8_t, 8> encoded_count{};
  if (!receive_all(socket, magic.data(), magic.size()) ||
      !receive_all(socket, encoded_rank.data(), encoded_rank.size()) ||
      !receive_all(socket, encoded_count.data(), encoded_count.size())) return false;
  rank = decode_u32(encoded_rank);
  count = decode_u64(encoded_count);
  return decode_u32(magic) == wire_magic;
}

bool master_allreduce(std::vector<double>& data, int world, int port) {
  const int listener = listen_socket(port);
  if (listener < 0) return false;
  std::vector<std::vector<double>> ranks(static_cast<std::size_t>(world));
  ranks[0] = data;
  std::vector<bool> seen(static_cast<std::size_t>(world), false);
  seen[0] = true;
  std::vector<int> peers;
  peers.reserve(static_cast<std::size_t>(world - 1));
  bool ok = true;
  for (int connected = 0; connected < world - 1 && ok; ++connected) {
    pollfd event{listener, POLLIN, 0};
    if (::poll(&event, 1, socket_timeout_seconds * 1000) <= 0) {
      ok = false;
      break;
    }
    const int peer = ::accept4(listener, nullptr, nullptr, SOCK_CLOEXEC);
    if (peer < 0) {
      ok = false;
      break;
    }
    set_socket_timeout(peer);
    std::uint32_t rank = 0;
    std::uint64_t count = 0;
    if (!receive_header(peer, rank, count) || rank == 0 || rank >= static_cast<std::uint32_t>(world) ||
        seen[rank] || count != data.size()) {
      ::close(peer);
      ok = false;
      break;
    }
    ranks[rank].resize(data.size());
    if (!receive_vector(peer, ranks[rank])) {
      ::close(peer);
      ok = false;
      break;
    }
    seen[rank] = true;
    peers.push_back(peer);
  }
  ::close(listener);
  if (ok) {
    for (int rank = 1; rank < world && ok; ++rank) ok = seen[static_cast<std::size_t>(rank)];
  }
  if (ok) {
    std::vector<double> sum(data.size(), 0.0);
    for (int rank = 0; rank < world; ++rank) {
      const auto& values = ranks[static_cast<std::size_t>(rank)];
      for (std::size_t i = 0; i < sum.size(); ++i) sum[i] += values[i];
    }
    data = sum;
    for (int peer : peers) {
      if (!send_header(peer, 0, sum.size()) || !send_vector(peer, sum)) ok = false;
    }
  }
  for (int peer : peers) ::close(peer);
  return ok;
}

bool worker_allreduce(std::vector<double>& data, int rank, const std::string& host, int port) {
  const int peer = connect_socket(host, port);
  if (peer < 0) return false;
  bool ok = send_header(peer, static_cast<std::uint32_t>(rank), data.size()) && send_vector(peer, data);
  std::uint32_t root = 1;
  std::uint64_t count = 0;
  if (ok) ok = receive_header(peer, root, count) && root == 0 && count == data.size();
  if (ok) ok = receive_vector(peer, data);
  ::close(peer);
  return ok;
}

int reserve_local_port() {
  const int socket = ::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
  if (socket < 0) return -1;
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  address.sin_port = 0;
  socklen_t length = sizeof(address);
  int port = -1;
  if (::bind(socket, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0 &&
      ::getsockname(socket, reinterpret_cast<sockaddr*>(&address), &length) == 0) {
    port = ntohs(address.sin_port);
  }
  ::close(socket);
  return port;
}

} // namespace

bool ax_dist_ring_allreduce(std::vector<double>& data, int rank, int world,
                            const std::string& host, int port) {
  if (world <= 0 || rank < 0 || rank >= world || port < 1 || port > 65535) return false;
  if (world == 1) return rank == 0;
  return rank == 0 ? master_allreduce(data, world, port)
                   : worker_allreduce(data, rank, host, port);
}

bool ax_dist_train_localhost(int workers, const std::string& ckpt_path) {
  if (workers <= 0 || workers > 64) return false;
  if (workers == 1) return ax_dist_checkpoint_write(ckpt_path, {1.0, 2.0, 3.0, 4.0});
  const int port = reserve_local_port();
  if (port < 0) return false;
  std::vector<pid_t> children;
  children.reserve(static_cast<std::size_t>(workers));
  bool spawn_ok = true;
  for (int rank = 0; rank < workers; ++rank) {
    const pid_t child = ::fork();
    if (child < 0) {
      spawn_ok = false;
      break;
    }
    if (child == 0) {
      std::vector<double> parameters{static_cast<double>(rank + 1),
                                     static_cast<double>((rank + 1) * 2),
                                     static_cast<double>((rank + 1) * 3),
                                     static_cast<double>((rank + 1) * 4)};
      const bool ok = ax_dist_ring_allreduce(parameters, rank, workers, "127.0.0.1", port) &&
                      (rank != 0 || ax_dist_checkpoint_write(ckpt_path, parameters));
      ::_exit(ok ? 0 : 1);
    }
    children.push_back(child);
  }
  if (!spawn_ok) {
    for (pid_t child : children) ::kill(child, SIGTERM);
  }
  bool success = spawn_ok;
  for (pid_t child : children) {
    int status = 0;
    if (::waitpid(child, &status, 0) != child || !WIFEXITED(status) || WEXITSTATUS(status) != 0) {
      success = false;
    }
  }
  std::vector<double> restored;
  return success && ax_dist_checkpoint_read(ckpt_path, restored) && restored.size() == 4;
}

} // namespace ax
