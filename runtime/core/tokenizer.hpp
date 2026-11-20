#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace ax::ai {

struct BpeMerge {
  std::uint32_t left;
  std::uint32_t right;
  std::uint32_t result;
  std::uint32_t rank;
};

struct EncodedToken {
  std::uint32_t id;
  std::string_view bytes;
};

class Tokenizer {
 public:
  static Tokenizer bpe(std::vector<std::string> vocabulary,
                       std::vector<BpeMerge> merges,
                       std::optional<std::uint32_t> unknown_id = std::nullopt);
  static Tokenizer unigram(std::vector<std::string> vocabulary,
                           std::vector<double> scores,
                           std::optional<std::uint32_t> unknown_id = std::nullopt,
                           double unknown_score = -1000.0);

  std::vector<EncodedToken> encode(std::string_view input) const;
  std::string decode(std::span<const std::uint32_t> token_ids) const;
  std::size_t vocabulary_size() const noexcept { return vocabulary_.size(); }

 private:
  enum class Model { Bpe, Unigram };
  struct MergeRule { std::uint32_t result; std::uint32_t rank; };
  struct TrieNode {
    std::unordered_map<unsigned char, std::size_t> children;
    std::optional<std::uint32_t> token_id;
  };

  Tokenizer(Model model, std::vector<std::string> vocabulary,
            std::optional<std::uint32_t> unknown_id);

  Model model_;
  std::vector<std::string> vocabulary_;
  std::optional<std::uint32_t> unknown_id_;
  std::vector<std::uint32_t> byte_ids_;
  std::vector<bool> has_byte_id_;
  std::unordered_map<std::uint64_t, MergeRule> merges_;
  std::vector<double> unigram_scores_;
  std::vector<TrieNode> trie_;
  double unknown_score_ = -1000.0;
};

} // namespace ax::ai
