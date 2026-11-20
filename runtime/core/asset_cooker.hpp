#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace ax::asset {

enum class Kind : std::uint32_t { Mesh = 1, Audio = 2, PngImage = 3 };

struct CookStats {
  Kind kind = Kind::Mesh;
  std::size_t source_bytes = 0;
  std::size_t output_bytes = 0;
  std::size_t vertices = 0;
  std::size_t triangles = 0;
  std::size_t meshlets = 0;
};

bool cook_asset_file(const std::string& input_path, const std::string& output_path,
                     CookStats& stats, std::string& error);
bool verify_asset_bundle(const std::string& path, std::string& error);

} // namespace ax::asset
