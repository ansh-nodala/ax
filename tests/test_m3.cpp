#include "runtime/core/ecs.hpp"
#include "runtime/core/asset_cooker.hpp"
#include "runtime/core/fixed.hpp"
#include "runtime/core/philox.hpp"
#include "runtime/core/physics.hpp"
#include "runtime/core/rendergraph.hpp"
#include "runtime/core/sha256.hpp"
#include "runtime/dist/dist.hpp"

#include <array>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <span>
#include <string>
#include <utility>
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

std::string hex(const std::array<std::uint8_t, 32>& digest) {
  constexpr char digits[] = "0123456789abcdef";
  std::string result;
  result.reserve(digest.size() * 2);
  for (std::uint8_t byte : digest) {
    result.push_back(digits[byte >> 4]);
    result.push_back(digits[byte & 0xf]);
  }
  return result;
}

void append_le16(std::vector<std::uint8_t>& bytes, std::uint16_t value) {
  bytes.push_back(static_cast<std::uint8_t>(value));
  bytes.push_back(static_cast<std::uint8_t>(value >> 8));
}

void append_le32(std::vector<std::uint8_t>& bytes, std::uint32_t value) {
  for (unsigned i = 0; i < 4; ++i) bytes.push_back(static_cast<std::uint8_t>(value >> (8 * i)));
}

void append_be32(std::vector<std::uint8_t>& bytes, std::uint32_t value) {
  for (int i = 3; i >= 0; --i) bytes.push_back(static_cast<std::uint8_t>(value >> (8 * i)));
}

std::uint32_t png_crc32(std::span<const std::uint8_t> bytes) {
  std::uint32_t crc = 0xffffffffu;
  for (std::uint8_t byte : bytes) {
    crc ^= byte;
    for (unsigned bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
  }
  return ~crc;
}

void append_png_chunk(std::vector<std::uint8_t>& png, const char (&name)[5],
                      std::span<const std::uint8_t> data) {
  append_be32(png, static_cast<std::uint32_t>(data.size()));
  std::vector<std::uint8_t> crc_input(name, name + 4);
  crc_input.insert(crc_input.end(), data.begin(), data.end());
  png.insert(png.end(), crc_input.begin(), crc_input.begin() + 4);
  png.insert(png.end(), data.begin(), data.end());
  append_be32(png, png_crc32(crc_input));
}

void write_bytes(const std::string& path, std::span<const std::uint8_t> bytes) {
  std::ofstream output(path, std::ios::binary);
  output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

struct Position { std::int32_t x = 0; };
struct Velocity { std::int32_t x = 0; };
struct Health { std::int32_t value = 100; };

using MoveSystem = ax::game::SystemAccess<ax::game::Reads<Velocity>, ax::game::Writes<Position>>;
using RenderSystem = ax::game::SystemAccess<ax::game::Reads<Velocity>, ax::game::Writes<Health>>;
using CompetingSystem = ax::game::SystemAccess<ax::game::Reads<Health>, ax::game::Writes<Position>>;
static_assert(!ax::game::systems_conflict_v<MoveSystem, RenderSystem>);
static_assert(ax::game::systems_conflict_v<MoveSystem, CompetingSystem>);

} // namespace

int main() {

  {
    ax::runtime::Sha256 sha;
    constexpr std::array<std::byte, 3> abc{std::byte{'a'}, std::byte{'b'}, std::byte{'c'}};
    sha.update(abc);
    check(hex(sha.finish()) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
          "SHA-256 matches the standard abc test vector");
  }

  {
    const ax::runtime::Philox4x32 random(0);
    const auto first = random.block(0);
    check(first == std::array<std::uint32_t, 4>{0x6627e8d5u, 0xe169c58du, 0xbc57ac4cu, 0x9b00dbd8u},
          "Philox4x32-10 matches the published zero counter vector");
    std::vector<std::uint32_t> one_worker(4096);
    std::vector<std::uint32_t> four_workers(4096);
    for (std::size_t i = 0; i < one_worker.size(); ++i) one_worker[i] = random.block(i)[0];
    for (std::size_t rank = 0; rank < 4; ++rank) {
      for (std::size_t i = rank; i < four_workers.size(); i += 4) four_workers[i] = random.block(i)[0];
    }
    check(one_worker == four_workers, "counter-based seed output is invariant to 1 vs 4 worker partitioning");
  }

  {
    using ax::game::fixed32;
    using ax::game::fixed64;
    const fixed32 three_halves = fixed32::from_ratio(3, 2);
    const fixed32 two = fixed32::from_integer(2);
    const fixed64 nine = fixed64::from_integer(9);
    check((three_halves * two).raw() == fixed32::from_integer(3).raw() &&
          fixed32::sqrt(fixed32::from_integer(4)).raw() == two.raw() &&
          fixed64::sqrt(nine).raw() == fixed64::from_integer(3).raw(),
          "Q16.16 and Q32.32 arithmetic and integer square root");
    check((fixed32::from_raw(INT32_MAX) + fixed32::from_integer(1)).raw() == INT32_MAX,
          "fixed-point addition saturates rather than overflowing");
    const fixed32 q16_angle = fixed32::from_integer(1);
    const fixed64 q32_angle = fixed64::from_integer(1);
    check(std::abs(fixed32::sin(q16_angle).to_double() - 0.8414709848) < 3.0e-4 &&
          std::abs(fixed32::cos(q16_angle).to_double() - 0.5403023059) < 3.0e-4 &&
          std::abs(fixed64::sin(q32_angle).to_double() - 0.8414709848) < 2.0e-8 &&
          std::abs(fixed64::cos(q32_angle).to_double() - 0.5403023059) < 2.0e-8,
          "Q16.16 and Q32.32 CORDIC sine and cosine");
  }

  {
    ax::game::RenderGraph graph;
    const auto a = graph.create_resource({128, 128, 1, 1, true});
    const auto b = graph.create_resource({128, 128, 1, 1, true});
    const auto c = graph.create_resource({128, 128, 1, 1, true});
    const auto write_a = graph.add_pass("write-a", {{a, ax::game::RenderAccess::StorageWrite}});
    const auto read_a_write_b = graph.add_pass("sample-a-write-b", {
        {a, ax::game::RenderAccess::ShaderRead}, {b, ax::game::RenderAccess::ColorWrite}});
    const auto read_b = graph.add_pass("sample-b", {{b, ax::game::RenderAccess::ShaderRead}});
    const auto write_c = graph.add_pass("write-c", {{c, ax::game::RenderAccess::StorageWrite}});
    graph.depends_on(write_c, read_b);
    const auto plan = graph.compile();
    const bool ordered = plan.pass_order == std::vector<ax::game::RenderPassId>{write_a, read_a_write_b,
                                                                                read_b, write_c};
    const bool barrier_chain = plan.barriers.size() == 2 &&
        plan.barriers[0].resource == a && plan.barriers[0].from == ax::game::RenderAccess::StorageWrite &&
        plan.barriers[0].to == ax::game::RenderAccess::ShaderRead &&
        plan.barriers[1].resource == b && plan.barriers[1].from == ax::game::RenderAccess::ColorWrite;
    const auto allocation_for = [&](ax::game::RenderResourceId resource) -> std::uint32_t {
      for (const auto& allocation : plan.allocations) {
        if (allocation.resource == resource) return allocation.alias_slot;
      }
      return UINT32_MAX;
    };
    check(ordered && barrier_chain, "render graph schedules hazards and emits write-to-read barriers");
    check(plan.transient_slot_count == 2 && allocation_for(a) == allocation_for(c) &&
          allocation_for(a) != allocation_for(b), "transient resources alias only after lifetimes end");

    ax::game::RenderGraph cyclic;
    const auto first = cyclic.add_pass("first", {});
    const auto second = cyclic.add_pass("second", {});
    cyclic.depends_on(first, second);
    cyclic.depends_on(second, first);
    bool detected = false;
    try { (void)cyclic.compile(); } catch (const std::logic_error&) { detected = true; }
    check(detected, "render graph rejects cyclic pass dependencies");
  }

  {
    ax::game::ArchetypeWorld<Position, Velocity, Health> world;
    constexpr std::size_t entity_count = 100000;
    for (std::size_t i = 0; i < entity_count; ++i) {
      world.create(Position{static_cast<std::int32_t>(i)}, Velocity{2}, Health{});
    }
    auto [positions, velocities] = world.query<Position, Velocity>();
    ax::game::parallel_for(entity_count, 4, [&](std::size_t begin, std::size_t end) {
      for (std::size_t i = begin; i < end; ++i) positions[i].x += velocities[i].x;
    });
    const bool all_updated = positions.size() == entity_count && positions[0].x == 2 &&
        positions[entity_count / 2].x == static_cast<std::int32_t>(entity_count / 2 + 2) &&
        positions.back().x == static_cast<std::int32_t>(entity_count - 1 + 2);
    check(world.size() == entity_count && all_updated,
          "100000-entity SoA archetype parallel update");
    const auto [health] = world.query<Health>();
    const ax::game::Entity stale = world.create(Position{}, Velocity{}, Health{});
    world.destroy(stale);
    const auto replacement = world.create(Position{}, Velocity{}, Health{});
    check(health.size() == entity_count && !world.alive(stale) && world.alive(replacement),
          "entity generation prevents stale-handle reuse");
  }

  {
    const ax::game::Vec3d a{2.0, 0.5, -0.25};
    const ax::game::Vec3d b{0.0, 0.0, 0.0};
    const auto spring = ax::game::spring_energy_gradient(a, b, 1.0, 4.0);
    constexpr double step = 1.0e-6;
    const auto plus = ax::game::spring_energy_gradient({a.x + step, a.y, a.z}, b, 1.0, 4.0);
    const auto minus = ax::game::spring_energy_gradient({a.x - step, a.y, a.z}, b, 1.0, 4.0);
    const double finite_difference = (plus.energy - minus.energy) / (2.0 * step);
    check(std::abs(spring.gradient_a.x - finite_difference) < 1.0e-7 &&
          spring.gradient_a.x + spring.gradient_b.x == 0.0,
          "spring physics gradient agrees with finite differences and conserves pair force");

    std::vector<ax::game::Triangle> triangles;
    for (int i = 0; i < 8; ++i) {
      const double x = static_cast<double>(i * 3);
      triangles.push_back({{x, 0.0, 0.0}, {x + 1.0, 0.0, 0.0}, {x, 1.0, 0.0}});
    }
    ax::game::TriangleBvh bvh;
    bvh.build(std::move(triangles));
    ax::game::RayHit hit{};
    const bool hit_expected = bvh.intersect({{9.25, 0.25, 2.0}, {0.0, 0.0, -1.0}}, hit);
    const bool limited_out = !bvh.intersect({{9.25, 0.25, 2.0}, {0.0, 0.0, -1.0}}, hit, 1.0);
    const bool miss = !bvh.intersect({{100.0, 100.0, 2.0}, {0.0, 0.0, -1.0}}, hit);
    check(hit_expected && hit.triangle_index == 3 && std::abs(hit.distance - 2.0) < 1.0e-12 &&
          bvh.node_count() > 1 && limited_out && miss,
          "BVH build, nearest ray hit, distance limit, and miss traversal");
  }

  {
    const std::string path = "/tmp/ax_m3_training.axckpt";
    const std::vector<double> expected{1.25, -2.5, 3.75, 0.0};
    const bool wrote = ax::ax_dist_checkpoint_write(path, expected);
    std::vector<double> loaded;
    std::array<std::uint8_t, 32> digest{};
    const bool verified = ax::ax_dist_checkpoint_verify(path, &digest);
    const bool read = ax::ax_dist_checkpoint_read(path, loaded);
    check(wrote && verified && read && loaded == expected &&
          digest != std::array<std::uint8_t, 32>{}, "atomic AXCKPT write, SHA-256 verify, and reload");
    if (wrote) {
      std::fstream file(path, std::ios::binary | std::ios::in | std::ios::out);
      file.seekg(-1, std::ios::end);
      char byte = 0;
      file.read(&byte, 1);
      file.clear();
      file.seekp(-1, std::ios::end);
      byte ^= 1;
      file.write(&byte, 1);
      file.close();
      check(!ax::ax_dist_checkpoint_verify(path), "checkpoint verification detects payload or digest corruption");
    } else {
      check(false, "checkpoint verification detects payload or digest corruption");
    }
    std::filesystem::remove(path);
    std::filesystem::remove(path + ".tmp." + std::to_string(static_cast<long long>(::getpid())));
  }

  {
    const std::string input = "/tmp/ax_m3_triangle.obj";
    const std::string output = "/tmp/ax_m3_triangle.axb";
    {
      std::ofstream obj(input);
      obj << "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n";
    }
    ax::asset::CookStats stats;
    std::string error;
    const bool cooked = ax::asset::cook_asset_file(input, output, stats, error);
    const bool verified = cooked && ax::asset::verify_asset_bundle(output, error);
    check(verified && stats.kind == ax::asset::Kind::Mesh && stats.vertices == 3 &&
          stats.triangles == 1 && stats.meshlets == 1 && stats.output_bytes > stats.source_bytes,
          "OBJ asset cooker emits a SHA-256 verified mesh bundle and meshlet");
    if (verified) {
      std::fstream bundle(output, std::ios::binary | std::ios::in | std::ios::out);
      bundle.seekg(64, std::ios::beg);
      char byte = 0;
      bundle.read(&byte, 1);
      bundle.clear();
      bundle.seekp(64, std::ios::beg);
      byte ^= 1;
      bundle.write(&byte, 1);
      bundle.close();
      check(!ax::asset::verify_asset_bundle(output, error), "asset bundle verifier detects payload corruption");
    } else {
      check(false, "asset bundle verifier detects payload corruption");
    }
    std::filesystem::remove(input);
    std::filesystem::remove(output);
  }

  {
    const std::string input = "/tmp/ax_m3_audio.wav";
    const std::string output = "/tmp/ax_m3_audio.axb";
    std::vector<std::uint8_t> wav{'R','I','F','F'};
    append_le32(wav, 40);
    wav.insert(wav.end(), {'W','A','V','E','f','m','t',' '});
    append_le32(wav, 16);
    append_le16(wav, 1); append_le16(wav, 1); append_le32(wav, 8000);
    append_le32(wav, 16000); append_le16(wav, 2); append_le16(wav, 16);
    wav.insert(wav.end(), {'d','a','t','a'});
    append_le32(wav, 4);
    wav.insert(wav.end(), {0, 0, 0xff, 0x7f});
    write_bytes(input, wav);
    ax::asset::CookStats stats;
    std::string error;
    const bool cooked = ax::asset::cook_asset_file(input, output, stats, error);
    check(cooked && stats.kind == ax::asset::Kind::Audio &&
          ax::asset::verify_asset_bundle(output, error), "PCM WAV parser emits a verified audio bundle");
    std::filesystem::remove(input);
    std::filesystem::remove(output);
  }

  {
    const std::string input = "/tmp/ax_m3_pixel.png";
    const std::string output = "/tmp/ax_m3_pixel.axb";
    constexpr std::array<std::uint8_t, 8> signature{137, 80, 78, 71, 13, 10, 26, 10};
    std::vector<std::uint8_t> png(signature.begin(), signature.end());
    std::array<std::uint8_t, 13> ihdr{0,0,0,1, 0,0,0,1, 8,6,0,0,0};
    append_png_chunk(png, "IHDR", ihdr);
    constexpr std::array<std::uint8_t, 16> compressed_pixel{
        0x78, 0x01, 0x01, 0x05, 0x00, 0xfa, 0xff, 0x00, 0x00, 0x00, 0x00, 0xff,
        0x01, 0x01, 0x01, 0x00};
    append_png_chunk(png, "IDAT", compressed_pixel);
    append_png_chunk(png, "IEND", {});
    write_bytes(input, png);
    ax::asset::CookStats stats;
    std::string error;
    const bool cooked = ax::asset::cook_asset_file(input, output, stats, error);
    check(cooked && stats.kind == ax::asset::Kind::PngImage &&
          ax::asset::verify_asset_bundle(output, error), "PNG structure and CRCs produce a verified image bundle");
    std::filesystem::remove(input);
    std::filesystem::remove(output);
  }

  {
    const std::string path = "/tmp/ax_m3_distributed.axckpt";
    const bool trained = ax::ax_dist_train_localhost(4, path);
    std::vector<double> loaded;
    const bool restored = ax::ax_dist_checkpoint_read(path, loaded);
    check(trained && restored && loaded == std::vector<double>({10.0, 20.0, 30.0, 40.0}),
          "four independent worker processes all-reduce and master checkpoint");
    check(ax::ax_dist_checkpoint_verify(path), "distributed master checkpoint passes SHA-256 verification");
    std::filesystem::remove(path);
  }

  std::cout << "RESULT: " << passed << " passed, " << failed << " failed\n";
  return failed == 0 ? 0 : 1;
}
