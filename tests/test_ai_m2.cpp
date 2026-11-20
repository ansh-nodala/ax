#include "runtime/core/ai_kernels.hpp"
#include "runtime/core/autodiff.hpp"
#include "runtime/core/axrt.hpp"
#include "runtime/core/kv_cache.hpp"
#include "runtime/core/model_io.hpp"
#include "runtime/core/quantized.hpp"
#include "runtime/core/tokenizer.hpp"

#include <cmath>
#include <exception>
#include <filesystem>
#include <iostream>
#include <limits>
#include <map>
#include <span>
#include <string>
#include <vector>

namespace {
int passed = 0;
int failed = 0;

void check(bool condition, const std::string& name) {
  if (condition) {
    ++passed;
    std::cout << "PASS " << name << '\n';
  } else {
    ++failed;
    std::cout << "FAIL " << name << '\n';
  }
}

bool close(float lhs, float rhs, float tolerance = 1.0e-4f) {
  return std::fabs(lhs - rhs) <= tolerance;
}

} // namespace

int main() {
  using ax::ai::LowBitFormat;

  const std::vector<float> trits{-1.0f, 0.0f, 1.0f, -1.0f, 1.0f, 0.0f};
  const auto packed_trits = ax::ai::pack_values(LowBitFormat::Trit, trits);
  const auto unpacked_trits = ax::ai::unpack_values(LowBitFormat::Trit, packed_trits, trits.size());
  check(packed_trits.size() == 2 && unpacked_trits == trits,
        "base-3 trit packing crosses a five-value byte boundary");

  const std::vector<float> bits{0.0f, 1.0f, 0.49f, 0.5f};
  const auto packed_bits = ax::ai::pack_values(LowBitFormat::I1, bits);
  check(packed_bits.size() == 1 &&
        ax::ai::unpack_values(LowBitFormat::I1, packed_bits, bits.size()) ==
            std::vector<float>({0.0f, 1.0f, 0.0f, 1.0f}),
        "i1 values are stored one bit per element");

  const std::vector<float> lhs_values{1.0f, 0.0f, -1.0f, 1.0f, 1.0f, 0.0f};
  const std::vector<float> rhs_values{1.0f, 0.0f, 0.0f, 1.0f, -1.0f, 1.0f};
  const auto lhs = ax::ai::pack_values(LowBitFormat::Trit, lhs_values);
  const auto rhs = ax::ai::pack_values(LowBitFormat::Trit, rhs_values);
  const auto product = ax::ai::trit_matmul(lhs, rhs, 2, 3, 2);
  check(product == std::vector<float>({2.0f, -1.0f, 1.0f, 1.0f}),
        "packed ternary matrix multiplication");

  const std::vector<float> i2_values{-2.0f, -1.0f, 0.0f, 1.0f};
  const std::vector<float> i4_values{-8.0f, -1.0f, 0.0f, 7.0f};
  check(ax::ai::unpack_values(LowBitFormat::I2,
                              ax::ai::pack_values(LowBitFormat::I2, i2_values), 4) == i2_values,
        "signed i2 pack/unpack");
  check(ax::ai::unpack_values(LowBitFormat::I4,
                              ax::ai::pack_values(LowBitFormat::I4, i4_values), 4) == i4_values,
        "signed i4 pack/unpack");

  const std::vector<float> fp4_values{0.0f, 0.5f, 1.0f, 1.5f, 2.0f, 3.0f, 4.0f, 6.0f};
  check(ax::ai::unpack_values(LowBitFormat::FP4E2M1,
                              ax::ai::pack_values(LowBitFormat::FP4E2M1, fp4_values), 8) == fp4_values,
        "FP4 E2M1 representable values");

  const std::vector<float> fp8_values{0.0f, 0.5f, 1.0f, -2.0f, 4.0f};
  const auto fp8_e4 = ax::ai::unpack_values(LowBitFormat::FP8E4M3,
      ax::ai::pack_values(LowBitFormat::FP8E4M3, fp8_values), fp8_values.size());
  const auto fp8_e5 = ax::ai::unpack_values(LowBitFormat::FP8E5M2,
      ax::ai::pack_values(LowBitFormat::FP8E5M2, fp8_values), fp8_values.size());
  check(fp8_e4.size() == fp8_values.size() && close(fp8_e4[3], -2.0f) && close(fp8_e4[4], 4.0f),
        "FP8 E4M3 finite value conversion");
  check(fp8_e5.size() == fp8_values.size() && close(fp8_e5[3], -2.0f) && close(fp8_e5[4], 4.0f),
        "FP8 E5M2 value conversion");
  const std::vector<float> fp8_specials{std::numeric_limits<float>::infinity(),
                                         std::numeric_limits<float>::quiet_NaN()};
  const auto e4_specials = ax::ai::unpack_values(LowBitFormat::FP8E4M3,
      ax::ai::pack_values(LowBitFormat::FP8E4M3, fp8_specials), fp8_specials.size());
  const auto e5_specials = ax::ai::unpack_values(LowBitFormat::FP8E5M2,
      ax::ai::pack_values(LowBitFormat::FP8E5M2, fp8_specials), fp8_specials.size());
  check(std::isfinite(e4_specials[0]) && std::isnan(e4_specials[1]) &&
        std::isinf(e5_specials[0]) && std::isnan(e5_specials[1]),
        "FP8 E4M3FN and E5M2 special encodings");

  const std::vector<float> bf16_values{1.5f, -2.25f, 0.125f};
  const auto bf16 = ax::ai::unpack_values(LowBitFormat::BF16,
      ax::ai::pack_values(LowBitFormat::BF16, bf16_values), bf16_values.size());
  check(bf16 == bf16_values, "BF16 round-to-nearest conversion");

  const std::vector<float> rope_input{1.0f, 0.0f, 1.0f, 0.0f};
  const auto rope_zero = ax::ai::rope(rope_input, 1, 4, 0);
  const auto rope_offset = ax::ai::rope(rope_input, 1, 4, 1);
  check(rope_zero == rope_input && close(rope_offset[0], std::cos(1.0f)) &&
        close(rope_offset[1], std::sin(1.0f)) && close(rope_offset[3], std::sin(0.01f)),
        "RoPE applies position offset across frequency bands");

  const std::vector<float> slopes{1.0f, 0.5f};
  const auto bias = ax::ai::alibi_bias(1, 3, slopes, 2, 0);
  check(bias == std::vector<float>({-2.0f, -1.0f, 0.0f, -1.0f, -0.5f, 0.0f}),
        "ALiBi causal distance bias");

  const std::vector<float> norm_input{3.0f, 4.0f};
  const auto normalized = ax::ai::rms_norm(norm_input, 1, 2, {}, 1.0e-12f);
  check(close(normalized[0], 0.8485281f) && close(normalized[1], 1.1313709f),
        "RMSNorm numerical values");
  const std::vector<float> gate{0.0f, 1.0f};
  const std::vector<float> up{2.0f, 3.0f};
  const auto swiglu = ax::ai::swiglu(gate, up);
  const auto geglu = ax::ai::geglu(gate, up);
  check(close(swiglu[0], 0.0f) && close(swiglu[1], 3.0f / (1.0f + std::exp(-1.0f))),
        "fused SwiGLU numerical values");
  check(close(geglu[0], 0.0f) && close(geglu[1], 2.5240684f),
        "fused GeGLU numerical values");
  double abi_rope_values[]{1.0, 1.0, 0.0, 1.0};
  AxVal* abi_rope_input = ax_tensor_vals(abi_rope_values, 4);
  AxVal* abi_rope_output = ax_rope(abi_rope_input);
  double abi_gate_values[]{0.0, 1.0};
  AxVal* abi_gate_input = ax_tensor_vals(abi_gate_values, 2);
  AxVal* abi_swiglu_output = ax_swiglu(abi_gate_input);
  double abi_up_values[]{2.0, 3.0};
  AxVal* abi_up_input = ax_tensor_vals(abi_up_values, 2);
  AxVal* abi_swiglu2_output = ax_swiglu2(abi_gate_input, abi_up_input);
  AxVal* abi_geglu_output = ax_geglu(abi_gate_input, abi_up_input);
  double abi_rotary_values[]{1.0, 0.0, 1.0, 0.0};
  AxVal* abi_rotary_input = ax_tensor_vals(abi_rotary_values, 4);
  AxVal* abi_offset_output = ax_rope_ex(abi_rotary_input, ax_scalar(1.0),
                                         ax_scalar(4.0), ax_scalar(1.0));
  check(abi_rope_output->len == 4 && close(static_cast<float>(abi_rope_output->arr[0]), 1.0f) &&
        close(static_cast<float>(abi_rope_output->arr[1]), 1.0f) &&
        abi_swiglu_output->len == 2 &&
        close(static_cast<float>(abi_swiglu_output->arr[1]), 1.0f / (1.0f + std::exp(-1.0f))) &&
        close(static_cast<float>(abi_swiglu2_output->arr[1]), 3.0f / (1.0f + std::exp(-1.0f))) &&
        close(static_cast<float>(abi_geglu_output->arr[1]), 2.5240684f) &&
        close(static_cast<float>(abi_offset_output->arr[0]), std::cos(1.0f)) &&
        close(static_cast<float>(abi_offset_output->arr[1]), std::sin(1.0f)),
        "AX runtime ABI routes position-aware RoPE and fused SwiGLU/GeGLU");

  const std::vector<float> q{1.0f};
  const std::vector<float> k{0.0f, std::log(3.0f)};
  const std::vector<float> v{2.0f, 4.0f};
  const auto attention = ax::ai::flash_attention(q, k, v, 1, 2, 1, false, 0, 0, 1);
  check(attention.size() == 1 && close(attention[0], 3.5f),
        "tiled online-softmax attention numerical result");

  const std::vector<float> gqa_query{1.0f, 1.0f};
  const auto gqa = ax::ai::grouped_query_attention(gqa_query, k, v, 1, 2, 2, 1, 1);
  check(gqa.size() == 2 && close(gqa[0], 3.5f) && close(gqa[1], 3.5f),
        "GQA shares one KV head across two query heads");

  const std::vector<float> moe_logits{4.0f, 1.0f, 0.0f, 0.0f, 3.0f, 1.0f};
  const auto routing = ax::ai::route_moe(moe_logits, 2, 3, 1);
  check(routing.expert_indices == std::vector<std::size_t>({0, 1}) &&
        routing.expert_weights == std::vector<float>({1.0f, 1.0f}) &&
        std::isfinite(routing.auxiliary_loss),
        "MoE top-1 routing and balance loss");

  const std::string safetensors_path = "/tmp/ax_m2_roundtrip.safetensors";
  const ax::ai::TensorBlob weights{"F32", {2}, {0x00, 0x00, 0x80, 0x3f,
                                                  0x00, 0x00, 0x00, 0xc0}};
  const ax::ai::TensorBlob bias_blob{"I16", {2}, {0x34, 0x12, 0xfe, 0xff}};
  ax::ai::write_safetensors(safetensors_path, {{"bias", bias_blob}, {"weights", weights}});
  const auto mapped = ax::ai::MappedSafeTensors::open(safetensors_path);
  const auto& mapped_weights = mapped.tensor("weights");
  const auto& mapped_bias = mapped.tensor("bias");
  const bool zero_copy = mapped.mapped_data() != nullptr &&
      mapped_weights.bytes.data() >= mapped.mapped_data() &&
      mapped_weights.bytes.data() < mapped.mapped_data() + mapped.mapped_size();
  check(mapped.contains("weights") && mapped_weights.dtype == "F32" &&
        mapped_weights.shape == std::vector<std::uint64_t>({2}) &&
        std::vector<std::uint8_t>(mapped_weights.bytes.begin(), mapped_weights.bytes.end()) == weights.bytes &&
        mapped_bias.dtype == "I16" &&
        std::vector<std::uint8_t>(mapped_bias.bytes.begin(), mapped_bias.bytes.end()) == bias_blob.bytes && zero_copy,
        "SafeTensors write and mmap read round-trip without tensor copies");
  std::filesystem::remove(safetensors_path);

  auto bpe = ax::ai::Tokenizer::bpe(
      {"a", "b", "ab", "abab"}, {{0, 1, 2, 0}, {2, 2, 3, 1}});
  const std::string bpe_input = "abab";
  const auto bpe_tokens = bpe.encode(bpe_input);
  const std::vector<std::uint32_t> bpe_ids{3};
  check(bpe_tokens.size() == 1 && bpe_tokens[0].id == 3 &&
        bpe_tokens[0].bytes.data() == bpe_input.data() &&
        bpe.decode(bpe_ids) == bpe_input,
        "ranked byte BPE returns zero-copy token spans and decodes exactly");

  auto unigram = ax::ai::Tokenizer::unigram(
      {"a", "b", "ab", "bc", "abc", "<unk>"}, {0.0, 0.0, 1.0, 0.0, -10.0, -1000.0}, 5);
  const auto unigram_tokens = unigram.encode("abc");
  check(unigram_tokens.size() == 2 && unigram_tokens[0].id == 0 &&
        unigram_tokens[1].id == 3 && unigram_tokens[0].bytes == "a" &&
        unigram_tokens[1].bytes == "bc",
        "Unigram Viterbi selects the highest-scoring zero-copy segmentation");

  ax::ai::PagedKvCache kv_cache(2, 2);
  const std::vector<float> key0{1.0f, 2.0f};
  const std::vector<float> value0{10.0f, 20.0f};
  const std::vector<float> key1{3.0f, 4.0f};
  const std::vector<float> value1{30.0f, 40.0f};
  const std::vector<float> key2{5.0f, 6.0f};
  const std::vector<float> value2{50.0f, 60.0f};
  kv_cache.append(key0, value0);
  kv_cache.append(key1, value1);
  kv_cache.append(key2, value2);
  ax::ai::RadixPrefixCache prefix_cache(2, 2);
  const std::vector<std::uint32_t> prefix_ids{10, 20, 30};
  prefix_cache.insert(prefix_ids, kv_cache);
  const std::vector<std::uint32_t> longer_ids{10, 20, 99};
  const auto hit = prefix_cache.longest_prefix(longer_ids);
  const bool shared_pages = hit && hit->token_ids == std::vector<std::uint32_t>({10, 20}) &&
      hit->cache.size() == 2 && hit->cache.key_page(0).data() == kv_cache.key_page(0).data();
  check(shared_pages, "radix prefix hit shares complete KV pages without copying");
  const std::vector<std::uint32_t> partial_ids{10, 99};
  auto partial_hit = prefix_cache.longest_prefix(partial_ids);
  const std::vector<float> key3{7.0f, 8.0f};
  const std::vector<float> value3{70.0f, 80.0f};
  if (partial_hit) partial_hit->cache.append(key3, value3);
  check(partial_hit && partial_hit->cache.size() == 2 &&
        partial_hit->cache.key_token(1)[0] == 7.0f && kv_cache.size() == 3 &&
        kv_cache.key_token(1)[0] == 3.0f && prefix_cache.prefix_count() == 3,
        "partial radix prefix append uses copy-on-write and preserves cached KV state");

  ax::ai::DiffGraph graph(2);
  const auto x = graph.input(0);
  const auto y = graph.input(1);
  const auto x_squared = graph.multiply(x, x);
  const auto polynomial = graph.multiply(x_squared, y);
  const auto sine = graph.sin(y);
  const auto exponential = graph.exp(x);
  const auto output = graph.add(graph.add(polynomial, sine), exponential);
  const auto product_node = graph.multiply(x, y);
  const std::vector<double> point{0.7, -0.3};
  const double step = 1.0e-5;
  const auto value_at = [&](const std::vector<double>& at) {
    return graph.evaluate(at, {output})[0];
  };
  const auto gradient = graph.vjp(point, output);
  std::vector<double> finite_gradient(2);
  for (std::size_t i = 0; i < point.size(); ++i) {
    auto plus = point;
    auto minus = point;
    plus[i] += step;
    minus[i] -= step;
    finite_gradient[i] = (value_at(plus) - value_at(minus)) / (2.0 * step);
  }
  const std::vector<double> direction{0.2, -0.4};
  auto plus_direction = point;
  auto minus_direction = point;
  for (std::size_t i = 0; i < point.size(); ++i) {
    plus_direction[i] += step * direction[i];
    minus_direction[i] -= step * direction[i];
  }
  const double finite_jvp = (value_at(plus_direction) - value_at(minus_direction)) / (2.0 * step);
  check(close(static_cast<float>(gradient[0]), static_cast<float>(finite_gradient[0]), 2.0e-5f) &&
        close(static_cast<float>(gradient[1]), static_cast<float>(finite_gradient[1]), 2.0e-5f) &&
        close(static_cast<float>(graph.jvp(point, output, direction)), static_cast<float>(finite_jvp), 2.0e-5f),
        "reverse VJP and forward JVP pass finite-difference checks");

  const auto jacobian = graph.jacobian(point, {output, product_node});
  const auto product_gradient = graph.vjp(point, product_node);
  check(jacobian.size() == 4 &&
        close(static_cast<float>(jacobian[0]), static_cast<float>(gradient[0])) &&
        close(static_cast<float>(jacobian[1]), static_cast<float>(gradient[1])) &&
        close(static_cast<float>(jacobian[2]), static_cast<float>(product_gradient[0])) &&
        close(static_cast<float>(jacobian[3]), static_cast<float>(product_gradient[1])),
        "Jacobian composes output-wise reverse-mode gradients");

  const auto hessian = graph.hessian(point, output);
  bool hessian_matches_finite_difference = hessian.size() == 4;
  for (std::size_t column = 0; column < point.size(); ++column) {
    auto plus = point;
    auto minus = point;
    plus[column] += step;
    minus[column] -= step;
    const auto gradient_plus = graph.vjp(plus, output);
    const auto gradient_minus = graph.vjp(minus, output);
    for (std::size_t row = 0; row < point.size(); ++row) {
      const double finite = (gradient_plus[row] - gradient_minus[row]) / (2.0 * step);
      hessian_matches_finite_difference = hessian_matches_finite_difference &&
          close(static_cast<float>(hessian[row * point.size() + column]),
                static_cast<float>(finite), 3.0e-5f);
    }
  }
  check(hessian_matches_finite_difference,
        "exact second-order Hessian passes finite-difference checks");

  std::cout << "RESULT: " << passed << " passed, " << failed << " failed\n";
  return failed == 0 ? 0 : 1;
}
