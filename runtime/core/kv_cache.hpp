#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <unordered_map>
#include <vector>

namespace ax::ai {

class PagedKvCache {
 public:
  struct Page {
    std::vector<float> keys;
    std::vector<float> values;
    std::size_t tokens = 0;
  };

  PagedKvCache(std::size_t page_size, std::size_t head_dimension);
  void append(std::span<const float> key, std::span<const float> value);
  PagedKvCache prefix(std::size_t token_count) const;

  std::size_t size() const noexcept { return token_count_; }
  std::size_t page_size() const noexcept { return page_size_; }
  std::size_t head_dimension() const noexcept { return head_dimension_; }
  std::size_t page_count() const noexcept { return pages_.size(); }
  std::span<const float> key_token(std::size_t token) const;
  std::span<const float> value_token(std::size_t token) const;
  std::span<const float> key_page(std::size_t page) const;
  std::span<const float> value_page(std::size_t page) const;

 private:
  std::size_t page_size_;
  std::size_t head_dimension_;
  std::size_t token_count_ = 0;
  std::vector<std::shared_ptr<Page>> pages_;
};

struct KvPrefixHit {
  std::vector<std::uint32_t> token_ids;
  PagedKvCache cache;
};

class RadixPrefixCache {
 public:
  explicit RadixPrefixCache(std::size_t page_size, std::size_t head_dimension);
  void insert(std::span<const std::uint32_t> token_ids,
              const PagedKvCache& cache);
  std::optional<KvPrefixHit> longest_prefix(
      std::span<const std::uint32_t> token_ids) const;
  std::size_t prefix_count() const;
  void clear();

 private:
  struct Node {
    std::unordered_map<std::uint32_t, std::unique_ptr<Node>> children;
    std::optional<PagedKvCache> cache;
  };

  std::size_t page_size_;
  std::size_t head_dimension_;
  mutable std::mutex mutex_;
  Node root_;
  std::size_t prefix_count_ = 0;
};

} // namespace ax::ai
