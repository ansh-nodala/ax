#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <vector>
namespace ax {
bool ax_dist_ring_allreduce(std::vector<double>& data, int rank, int world, const std::string& host, int port);
bool ax_dist_checkpoint_write(const std::string& path, const std::vector<double>& params);
bool ax_dist_checkpoint_read(const std::string& path, std::vector<double>& params);
bool ax_dist_checkpoint_verify(const std::string& path,
                               std::array<std::uint8_t, 32>* digest = nullptr);
bool ax_dist_train_localhost(int workers, const std::string& ckpt_path);
} // namespace ax
