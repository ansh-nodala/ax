#pragma once

#include <cstddef>
#include <span>
#include <vector>

namespace ax::ai {

std::vector<float> rope(std::span<const float> input, std::size_t sequence_length,
                        std::size_t head_dimension, std::size_t position_offset = 0,
                        float theta = 10000.0f);
std::vector<float> alibi_bias(std::size_t query_length, std::size_t key_length,
                              std::span<const float> slopes,
                              std::size_t query_position_offset = 0,
                              std::size_t key_position_offset = 0);
std::vector<float> rms_norm(std::span<const float> input, std::size_t rows,
                            std::size_t columns, std::span<const float> gamma = {},
                            float epsilon = 1.0e-6f);
std::vector<float> swiglu(std::span<const float> gate, std::span<const float> up);
std::vector<float> geglu(std::span<const float> gate, std::span<const float> up);
std::vector<float> flash_attention(std::span<const float> query,
                                   std::span<const float> key,
                                   std::span<const float> value,
                                   std::size_t query_length,
                                   std::size_t key_length,
                                   std::size_t head_dimension,
                                   bool causal = false,
                                   std::size_t query_position_offset = 0,
                                   std::size_t key_position_offset = 0,
                                   std::size_t key_tile = 64);
std::vector<float> grouped_query_attention(std::span<const float> query,
                                           std::span<const float> key,
                                           std::span<const float> value,
                                           std::size_t query_length,
                                           std::size_t key_length,
                                           std::size_t query_heads,
                                           std::size_t key_value_heads,
                                           std::size_t head_dimension,
                                           bool causal = false,
                                           std::size_t query_position_offset = 0,
                                           std::size_t key_position_offset = 0);

struct MoeRouting {
  std::vector<std::size_t> expert_indices;
  std::vector<float> expert_weights;
  float auxiliary_loss = 0.0f;
};

MoeRouting route_moe(std::span<const float> logits, std::size_t token_count,
                     std::size_t expert_count, std::size_t top_k);

} // namespace ax::ai
