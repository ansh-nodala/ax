#include "runtime/core/kv_cache.hpp"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace ax::ai {

PagedKvCache::PagedKvCache(std::size_t page_size, std::size_t head_dimension)
    : page_size_(page_size), head_dimension_(head_dimension) {
  if (page_size_ == 0 || head_dimension_ == 0) {
    throw std::invalid_argument("KV cache page size and head dimension must be positive");
  }
}

void PagedKvCache::append(std::span<const float> key,
                          std::span<const float> value) {
  if (key.size() != head_dimension_ || value.size() != head_dimension_) {
    throw std::invalid_argument("KV cache token width does not match head dimension");
  }
  const std::size_t in_page = token_count_ % page_size_;
  if (in_page == 0) {
    pages_.push_back(std::make_shared<Page>());
    pages_.back()->keys.reserve(page_size_ * head_dimension_);
    pages_.back()->values.reserve(page_size_ * head_dimension_);
  } else if (pages_.back().use_count() > 1 || pages_.back()->tokens != in_page) {
    auto private_page = std::make_shared<Page>(*pages_.back());
    private_page->tokens = in_page;
    private_page->keys.resize(in_page * head_dimension_);
    private_page->values.resize(in_page * head_dimension_);
    pages_.back() = std::move(private_page);
  }
  Page& page = *pages_.back();
  page.keys.insert(page.keys.end(), key.begin(), key.end());
  page.values.insert(page.values.end(), value.begin(), value.end());
  ++page.tokens;
  ++token_count_;
}

PagedKvCache PagedKvCache::prefix(std::size_t token_count) const {
  if (token_count > token_count_) throw std::out_of_range("KV cache prefix exceeds stored tokens");
  PagedKvCache result(page_size_, head_dimension_);
  result.token_count_ = token_count;
  const std::size_t pages = token_count == 0 ? 0 : (token_count + page_size_ - 1) / page_size_;
  result.pages_.insert(result.pages_.end(), pages_.begin(), pages_.begin() + static_cast<std::ptrdiff_t>(pages));
  return result;
}

std::span<const float> PagedKvCache::key_token(std::size_t token) const {
  if (token >= token_count_) throw std::out_of_range("KV key token index is out of range");
  const Page& page = *pages_[token / page_size_];
  return std::span<const float>(page.keys).subspan((token % page_size_) * head_dimension_, head_dimension_);
}

std::span<const float> PagedKvCache::value_token(std::size_t token) const {
  if (token >= token_count_) throw std::out_of_range("KV value token index is out of range");
  const Page& page = *pages_[token / page_size_];
  return std::span<const float>(page.values).subspan((token % page_size_) * head_dimension_, head_dimension_);
}

std::span<const float> PagedKvCache::key_page(std::size_t page_index) const {
  if (page_index >= pages_.size()) throw std::out_of_range("KV cache page index is out of range");
  const std::size_t first_token = page_index * page_size_;
  const std::size_t visible_tokens = std::min(page_size_, token_count_ - first_token);
  return std::span<const float>(pages_[page_index]->keys).first(visible_tokens * head_dimension_);
}

std::span<const float> PagedKvCache::value_page(std::size_t page_index) const {
  if (page_index >= pages_.size()) throw std::out_of_range("KV cache page index is out of range");
  const std::size_t first_token = page_index * page_size_;
  const std::size_t visible_tokens = std::min(page_size_, token_count_ - first_token);
  return std::span<const float>(pages_[page_index]->values).first(visible_tokens * head_dimension_);
}

RadixPrefixCache::RadixPrefixCache(std::size_t page_size,
                                   std::size_t head_dimension)
    : page_size_(page_size), head_dimension_(head_dimension) {
  if (page_size_ == 0 || head_dimension_ == 0) {
    throw std::invalid_argument("radix cache page size and head dimension must be positive");
  }
}

void RadixPrefixCache::insert(std::span<const std::uint32_t> token_ids,
                              const PagedKvCache& cache) {
  if (token_ids.size() != cache.size() || cache.page_size() != page_size_ ||
      cache.head_dimension() != head_dimension_) {
    throw std::invalid_argument("radix prefix tokens and KV cache dimensions must match");
  }
  std::lock_guard<std::mutex> lock(mutex_);
  Node* node = &root_;
  for (std::size_t i = 0; i < token_ids.size(); ++i) {
    auto [it, inserted] = node->children.try_emplace(token_ids[i]);
    if (inserted) it->second = std::make_unique<Node>();
    node = it->second.get();
    if (!node->cache) ++prefix_count_;
    node->cache = cache.prefix(i + 1);
  }
}

std::optional<KvPrefixHit> RadixPrefixCache::longest_prefix(
    std::span<const std::uint32_t> token_ids) const {
  std::lock_guard<std::mutex> lock(mutex_);
  const Node* node = &root_;
  std::optional<KvPrefixHit> best;
  for (std::size_t i = 0; i < token_ids.size(); ++i) {
    const auto child = node->children.find(token_ids[i]);
    if (child == node->children.end()) break;
    node = child->second.get();
    if (node->cache) {
      best = KvPrefixHit{
          std::vector<std::uint32_t>(token_ids.begin(),
                                     token_ids.begin() + static_cast<std::ptrdiff_t>(i + 1)),
          *node->cache};
    }
  }
  return best;
}

std::size_t RadixPrefixCache::prefix_count() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return prefix_count_;
}

void RadixPrefixCache::clear() {
  std::lock_guard<std::mutex> lock(mutex_);
  root_ = Node{};
  prefix_count_ = 0;
}

} // namespace ax::ai
