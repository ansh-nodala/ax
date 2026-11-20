#pragma once

#include <cstdint>
#include <cstddef>
#include <map>
#include <span>
#include <string>
#include <vector>

namespace ax::ai {

struct TensorBlob {
  std::string dtype;
  std::vector<std::uint64_t> shape;
  std::vector<std::uint8_t> bytes;
};

struct TensorView {
  std::string dtype;
  std::vector<std::uint64_t> shape;
  std::span<const std::uint8_t> bytes;
};

void write_safetensors(const std::string& path,
                       const std::map<std::string, TensorBlob>& tensors);

class MappedSafeTensors {
 public:
  MappedSafeTensors() = default;
  ~MappedSafeTensors();
  MappedSafeTensors(const MappedSafeTensors&) = delete;
  MappedSafeTensors& operator=(const MappedSafeTensors&) = delete;
  MappedSafeTensors(MappedSafeTensors&& other) noexcept;
  MappedSafeTensors& operator=(MappedSafeTensors&& other) noexcept;

  static MappedSafeTensors open(const std::string& path);
  const TensorView& tensor(const std::string& name) const;
  bool contains(const std::string& name) const;
  const std::uint8_t* mapped_data() const { return data_; }
  std::size_t mapped_size() const { return size_; }

 private:
  void close() noexcept;
  const std::uint8_t* data_ = nullptr;
  std::size_t size_ = 0;
  int fd_ = -1;
  std::map<std::string, TensorView> tensors_;
};

} // namespace ax::ai
