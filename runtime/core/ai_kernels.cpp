#include "runtime/core/ai_kernels.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

#if defined(__aarch64__)
extern "C" float ax_neon_dot_f32(const float* lhs, const float* rhs, std::size_t count);
#endif

namespace ax::ai {
namespace {

void require_size(std::span<const float> values, std::size_t expected,
                  const char* name) {
  if (values.size() != expected) throw std::invalid_argument(name);
}

double dot_product(std::span<const float> lhs, std::span<const float> rhs) {
#if defined(__aarch64__)
  return ax_neon_dot_f32(lhs.data(), rhs.data(), lhs.size());
#else
  double sum = 0.0;
  for (std::size_t i = 0; i < lhs.size(); ++i) sum += static_cast<double>(lhs[i]) * rhs[i];
  return sum;
#endif
}

std::vector<float> attention_one_head(std::span<const float> query,
                                     std::span<const float> key,
                                     std::span<const float> value,
                                     std::size_t query_length,
                                     std::size_t key_length,
                                     std::size_t dimension,
                                     bool causal,
                                     std::size_t query_position_offset,
                                     std::size_t key_position_offset,
                                     std::size_t key_tile) {
  if (dimension == 0 || key_tile == 0) throw std::invalid_argument("attention dimensions must be nonzero");
  require_size(query, query_length * dimension, "query size does not match its shape");
  require_size(key, key_length * dimension, "key size does not match its shape");
  require_size(value, key_length * dimension, "value size does not match its shape");
  std::vector<float> output(query_length * dimension, 0.0f);
  const double scale = 1.0 / std::sqrt(static_cast<double>(dimension));
  std::vector<double> accumulator(dimension);
  std::vector<double> scores(key_tile);

  for (std::size_t q = 0; q < query_length; ++q) {
    double running_max = -std::numeric_limits<double>::infinity();
    double running_sum = 0.0;
    std::fill(accumulator.begin(), accumulator.end(), 0.0);
    const std::size_t absolute_query = query_position_offset + q;
    for (std::size_t tile_begin = 0; tile_begin < key_length; tile_begin += key_tile) {
      const std::size_t tile_end = std::min(key_length, tile_begin + key_tile);
      std::size_t score_count = 0;
      double tile_max = -std::numeric_limits<double>::infinity();
      for (std::size_t k = tile_begin; k < tile_end; ++k) {
        if (causal && key_position_offset + k > absolute_query) continue;
        const double dot = dot_product(
            query.subspan(q * dimension, dimension),
            key.subspan(k * dimension, dimension));
        const double score = dot * scale;
        scores[score_count++] = score;
        tile_max = std::max(tile_max, score);
      }
      if (score_count == 0) continue;

      const double next_max = std::max(running_max, tile_max);
      const double old_scale = std::isfinite(running_max) ? std::exp(running_max - next_max) : 0.0;
      running_sum *= old_scale;
      for (double& item : accumulator) item *= old_scale;

      std::size_t score_index = 0;
      for (std::size_t k = tile_begin; k < tile_end; ++k) {
        if (causal && key_position_offset + k > absolute_query) continue;
        const double weight = std::exp(scores[score_index++] - next_max);
        running_sum += weight;
        for (std::size_t d = 0; d < dimension; ++d) {
          accumulator[d] += weight * value[k * dimension + d];
        }
      }
      running_max = next_max;
    }
    if (running_sum == 0.0) continue;
    for (std::size_t d = 0; d < dimension; ++d) {
      output[q * dimension + d] = static_cast<float>(accumulator[d] / running_sum);
    }
  }
  return output;
}

} // namespace

std::vector<float> rope(std::span<const float> input, std::size_t sequence_length,
                        std::size_t head_dimension, std::size_t position_offset,
                        float theta) {
  require_size(input, sequence_length * head_dimension, "RoPE input size does not match its shape");
  if (head_dimension == 0 || head_dimension % 2 != 0 || theta <= 0.0f) {
    throw std::invalid_argument("RoPE requires a positive even head dimension and theta");
  }
  std::vector<float> output(input.begin(), input.end());
  for (std::size_t token = 0; token < sequence_length; ++token) {
    const double position = static_cast<double>(position_offset + token);
    for (std::size_t pair = 0; pair < head_dimension / 2; ++pair) {
      const double frequency = std::pow(static_cast<double>(theta),
                                        -2.0 * static_cast<double>(pair) /
                                            static_cast<double>(head_dimension));
      const double angle = position * frequency;
      const double cosine = std::cos(angle);
      const double sine = std::sin(angle);
      const std::size_t base = token * head_dimension + pair * 2;
      const double x = input[base];
      const double y = input[base + 1];
      output[base] = static_cast<float>(x * cosine - y * sine);
      output[base + 1] = static_cast<float>(x * sine + y * cosine);
    }
  }
  return output;
}

std::vector<float> alibi_bias(std::size_t query_length, std::size_t key_length,
                              std::span<const float> slopes,
                              std::size_t query_position_offset,
                              std::size_t key_position_offset) {
  if (slopes.empty()) throw std::invalid_argument("ALiBi requires at least one head slope");
  std::vector<float> bias(slopes.size() * query_length * key_length);
  for (std::size_t head = 0; head < slopes.size(); ++head) {
    for (std::size_t q = 0; q < query_length; ++q) {
      const long query_pos = static_cast<long>(query_position_offset + q);
      for (std::size_t k = 0; k < key_length; ++k) {
        const long key_pos = static_cast<long>(key_position_offset + k);
        const std::size_t index = (head * query_length + q) * key_length + k;
        bias[index] = slopes[head] * static_cast<float>(key_pos - query_pos);
      }
    }
  }
  return bias;
}

std::vector<float> rms_norm(std::span<const float> input, std::size_t rows,
                            std::size_t columns, std::span<const float> gamma,
                            float epsilon) {
  require_size(input, rows * columns, "RMSNorm input size does not match its shape");
  if (columns == 0 || epsilon <= 0.0f) throw std::invalid_argument("RMSNorm dimensions and epsilon must be positive");
  if (!gamma.empty()) require_size(gamma, columns, "RMSNorm gamma size does not match the row width");
  std::vector<float> output(input.size());
  for (std::size_t row = 0; row < rows; ++row) {
    double square_sum = 0.0;
    for (std::size_t column = 0; column < columns; ++column) {
      const double value = input[row * columns + column];
      square_sum += value * value;
    }
    const double inverse_rms = 1.0 / std::sqrt(square_sum / columns + epsilon);
    for (std::size_t column = 0; column < columns; ++column) {
      const std::size_t index = row * columns + column;
      const float weight = gamma.empty() ? 1.0f : gamma[column];
      output[index] = static_cast<float>(input[index] * inverse_rms * weight);
    }
  }
  return output;
}

std::vector<float> swiglu(std::span<const float> gate, std::span<const float> up) {
  require_size(up, gate.size(), "SwiGLU gate and up tensors must have equal size");
  std::vector<float> output(gate.size());
  for (std::size_t i = 0; i < gate.size(); ++i) {
    const float x = gate[i];
    const float sigmoid = x >= 0.0f ? 1.0f / (1.0f + std::exp(-x))
                                    : std::exp(x) / (1.0f + std::exp(x));
    output[i] = (x * sigmoid) * up[i];
  }
  return output;
}

std::vector<float> geglu(std::span<const float> gate, std::span<const float> up) {
  require_size(up, gate.size(), "GeGLU gate and up tensors must have equal size");
  constexpr double inverse_sqrt_two = 0.7071067811865475244;
  std::vector<float> output(gate.size());
  for (std::size_t i = 0; i < gate.size(); ++i) {
    const double x = gate[i];
    const double gelu = 0.5 * x * (1.0 + std::erf(x * inverse_sqrt_two));
    output[i] = static_cast<float>(gelu * up[i]);
  }
  return output;
}

std::vector<float> flash_attention(std::span<const float> query,
                                   std::span<const float> key,
                                   std::span<const float> value,
                                   std::size_t query_length,
                                   std::size_t key_length,
                                   std::size_t head_dimension,
                                   bool causal,
                                   std::size_t query_position_offset,
                                   std::size_t key_position_offset,
                                   std::size_t key_tile) {
  return attention_one_head(query, key, value, query_length, key_length,
                            head_dimension, causal, query_position_offset,
                            key_position_offset, key_tile);
}

std::vector<float> grouped_query_attention(std::span<const float> query,
                                           std::span<const float> key,
                                           std::span<const float> value,
                                           std::size_t query_length,
                                           std::size_t key_length,
                                           std::size_t query_heads,
                                           std::size_t key_value_heads,
                                           std::size_t head_dimension,
                                           bool causal,
                                           std::size_t query_position_offset,
                                           std::size_t key_position_offset) {
  if (query_heads == 0 || key_value_heads == 0 || query_heads % key_value_heads != 0) {
    throw std::invalid_argument("GQA query heads must be a positive multiple of KV heads");
  }
  require_size(query, query_length * query_heads * head_dimension, "GQA query size does not match its shape");
  require_size(key, key_length * key_value_heads * head_dimension, "GQA key size does not match its shape");
  require_size(value, key_length * key_value_heads * head_dimension, "GQA value size does not match its shape");
  std::vector<float> output(query.size());
  const std::size_t group_size = query_heads / key_value_heads;
  for (std::size_t qh = 0; qh < query_heads; ++qh) {
    const std::size_t kvh = qh / group_size;
    const auto q = query.subspan(qh * query_length * head_dimension, query_length * head_dimension);
    const auto k = key.subspan(kvh * key_length * head_dimension, key_length * head_dimension);
    const auto v = value.subspan(kvh * key_length * head_dimension, key_length * head_dimension);
    auto head_output = attention_one_head(q, k, v, query_length, key_length,
                                          head_dimension, causal,
                                          query_position_offset, key_position_offset, 64);
    std::copy(head_output.begin(), head_output.end(),
              output.begin() + static_cast<std::ptrdiff_t>(qh * query_length * head_dimension));
  }
  return output;
}

MoeRouting route_moe(std::span<const float> logits, std::size_t token_count,
                     std::size_t expert_count, std::size_t top_k) {
  require_size(logits, token_count * expert_count, "MoE logits size does not match its shape");
  if (expert_count == 0 || top_k == 0 || top_k > expert_count) {
    throw std::invalid_argument("MoE top_k must be between one and expert_count");
  }
  MoeRouting result;
  result.expert_indices.reserve(token_count * top_k);
  result.expert_weights.reserve(token_count * top_k);
  std::vector<double> mean_probability(expert_count, 0.0);
  std::vector<std::size_t> assignment_count(expert_count, 0);
  std::vector<double> probabilities(expert_count);
  for (std::size_t token = 0; token < token_count; ++token) {
    const auto row = logits.subspan(token * expert_count, expert_count);
    const float maximum = *std::max_element(row.begin(), row.end());
    double sum = 0.0;
    for (std::size_t expert = 0; expert < expert_count; ++expert) {
      probabilities[expert] = std::exp(static_cast<double>(row[expert] - maximum));
      sum += probabilities[expert];
    }
    for (std::size_t expert = 0; expert < expert_count; ++expert) {
      probabilities[expert] /= sum;
      mean_probability[expert] += probabilities[expert];
    }
    std::vector<std::size_t> order(expert_count);
    for (std::size_t expert = 0; expert < expert_count; ++expert) order[expert] = expert;
    std::partial_sort(order.begin(), order.begin() + static_cast<std::ptrdiff_t>(top_k), order.end(),
                      [&](std::size_t lhs, std::size_t rhs) {
                        if (probabilities[lhs] == probabilities[rhs]) return lhs < rhs;
                        return probabilities[lhs] > probabilities[rhs];
                      });
    double selected_sum = 0.0;
    for (std::size_t i = 0; i < top_k; ++i) selected_sum += probabilities[order[i]];
    for (std::size_t i = 0; i < top_k; ++i) {
      const std::size_t expert = order[i];
      result.expert_indices.push_back(expert);
      result.expert_weights.push_back(static_cast<float>(probabilities[expert] / selected_sum));
      ++assignment_count[expert];
    }
  }
  if (token_count > 0) {
    double aux = 0.0;
    for (std::size_t expert = 0; expert < expert_count; ++expert) {
      const double probability_fraction = mean_probability[expert] / token_count;
      const double assignment_fraction = static_cast<double>(assignment_count[expert]) /
                                         static_cast<double>(token_count * top_k);
      aux += probability_fraction * assignment_fraction;
    }
    result.auxiliary_loss = static_cast<float>(expert_count * aux);
  }
  return result;
}

} // namespace ax::ai
