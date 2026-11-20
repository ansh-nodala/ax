#include "runtime/core/tokenizer.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <queue>
#include <stdexcept>
#include <utility>

namespace ax::ai {
namespace {

std::uint64_t merge_key(std::uint32_t left, std::uint32_t right) {
  return (static_cast<std::uint64_t>(left) << 32) | right;
}

} // namespace

Tokenizer::Tokenizer(Model model, std::vector<std::string> vocabulary,
                     std::optional<std::uint32_t> unknown_id)
    : model_(model), vocabulary_(std::move(vocabulary)), unknown_id_(unknown_id),
      byte_ids_(256, 0), has_byte_id_(256, false) {
  if (vocabulary_.empty()) throw std::invalid_argument("tokenizer vocabulary cannot be empty");
  if (vocabulary_.size() > std::numeric_limits<std::uint32_t>::max()) {
    throw std::invalid_argument("tokenizer vocabulary exceeds the token ID range");
  }
  if (unknown_id_ && *unknown_id_ >= vocabulary_.size()) {
    throw std::invalid_argument("tokenizer unknown ID is outside the vocabulary");
  }
  for (std::size_t id = 0; id < vocabulary_.size(); ++id) {
    if (vocabulary_[id].empty()) throw std::invalid_argument("tokenizer vocabulary contains an empty token");
    if (vocabulary_[id].size() == 1) {
      const auto byte = static_cast<unsigned char>(vocabulary_[id][0]);
      if (has_byte_id_[byte]) throw std::invalid_argument("tokenizer vocabulary has duplicate byte tokens");
      byte_ids_[byte] = static_cast<std::uint32_t>(id);
      has_byte_id_[byte] = true;
    }
  }
}

Tokenizer Tokenizer::bpe(std::vector<std::string> vocabulary,
                         std::vector<BpeMerge> merges,
                         std::optional<std::uint32_t> unknown_id) {
  Tokenizer tokenizer(Model::Bpe, std::move(vocabulary), unknown_id);
  for (const BpeMerge& merge : merges) {
    if (merge.left >= tokenizer.vocabulary_.size() ||
        merge.right >= tokenizer.vocabulary_.size() ||
        merge.result >= tokenizer.vocabulary_.size()) {
      throw std::invalid_argument("BPE merge references a token outside the vocabulary");
    }
    if (tokenizer.vocabulary_[merge.left] + tokenizer.vocabulary_[merge.right] !=
        tokenizer.vocabulary_[merge.result]) {
      throw std::invalid_argument("BPE merge result does not concatenate its input tokens");
    }
    if (!tokenizer.merges_.emplace(merge_key(merge.left, merge.right),
                                   MergeRule{merge.result, merge.rank}).second) {
      throw std::invalid_argument("BPE model contains a duplicate merge pair");
    }
  }
  return tokenizer;
}

Tokenizer Tokenizer::unigram(std::vector<std::string> vocabulary,
                             std::vector<double> scores,
                             std::optional<std::uint32_t> unknown_id,
                             double unknown_score) {
  Tokenizer tokenizer(Model::Unigram, std::move(vocabulary), unknown_id);
  if (scores.size() != tokenizer.vocabulary_.size()) {
    throw std::invalid_argument("Unigram score count does not match the vocabulary");
  }
  if (!std::isfinite(unknown_score)) throw std::invalid_argument("Unigram unknown score must be finite");
  tokenizer.unigram_scores_ = std::move(scores);
  tokenizer.unknown_score_ = unknown_score;
  tokenizer.trie_.emplace_back();
  for (std::size_t id = 0; id < tokenizer.vocabulary_.size(); ++id) {
    if (!std::isfinite(tokenizer.unigram_scores_[id])) {
      throw std::invalid_argument("Unigram token scores must be finite");
    }
    std::size_t node = 0;
    for (unsigned char byte : tokenizer.vocabulary_[id]) {
      const std::size_t child_index = tokenizer.trie_.size();
      auto [it, inserted] = tokenizer.trie_[node].children.emplace(byte, child_index);
      const std::size_t next_node = it->second;
      if (inserted) tokenizer.trie_.emplace_back();
      node = next_node;
    }
    if (tokenizer.trie_[node].token_id) throw std::invalid_argument("Unigram vocabulary has duplicate tokens");
    tokenizer.trie_[node].token_id = static_cast<std::uint32_t>(id);
  }
  return tokenizer;
}

std::vector<EncodedToken> Tokenizer::encode(std::string_view input) const {
  if (input.empty()) return {};
  if (model_ == Model::Bpe) {
    struct Node {
      std::size_t begin;
      std::size_t length;
      std::size_t previous;
      std::size_t next;
      std::uint32_t id;
      std::uint64_t version = 0;
      bool alive = true;
      bool unknown = false;
    };
    struct Candidate {
      std::uint32_t rank;
      std::size_t left;
      std::uint64_t left_version;
      std::uint64_t right_version;
    };
    struct LaterMerge {
      bool operator()(const Candidate& lhs, const Candidate& rhs) const {
        if (lhs.rank != rhs.rank) return lhs.rank > rhs.rank;
        return lhs.left > rhs.left;
      }
    };

    const std::size_t none = std::numeric_limits<std::size_t>::max();
    std::vector<Node> nodes;
    nodes.reserve(input.size());
    for (std::size_t i = 0; i < input.size(); ++i) {
      const auto byte = static_cast<unsigned char>(input[i]);
      if (!has_byte_id_[byte] && !unknown_id_) {
        throw std::invalid_argument("BPE byte is missing from the vocabulary and no unknown token is set");
      }
      const bool unknown = !has_byte_id_[byte];
      nodes.push_back(Node{i, 1, i == 0 ? none : i - 1,
                           i + 1 == input.size() ? none : i + 1,
                           unknown ? *unknown_id_ : byte_ids_[byte], 0, true, unknown});
    }
    std::priority_queue<Candidate, std::vector<Candidate>, LaterMerge> candidates;
    const auto enqueue = [&](std::size_t left, auto& heap) {
      if (left == none || !nodes[left].alive || nodes[left].unknown) return;
      const std::size_t right = nodes[left].next;
      if (right == none || !nodes[right].alive || nodes[right].unknown) return;
      const auto rule = merges_.find(merge_key(nodes[left].id, nodes[right].id));
      if (rule != merges_.end()) {
        heap.push(Candidate{rule->second.rank, left, nodes[left].version, nodes[right].version});
      }
    };
    for (std::size_t i = 0; i + 1 < nodes.size(); ++i) enqueue(i, candidates);
    while (!candidates.empty()) {
      const Candidate candidate = candidates.top();
      candidates.pop();
      Node& left = nodes[candidate.left];
      if (!left.alive || left.version != candidate.left_version) continue;
      const std::size_t right_index = left.next;
      if (right_index == none) continue;
      Node& right = nodes[right_index];
      if (!right.alive || right.version != candidate.right_version) continue;
      const auto rule = merges_.find(merge_key(left.id, right.id));
      if (rule == merges_.end() || rule->second.rank != candidate.rank) continue;
      left.id = rule->second.result;
      left.length += right.length;
      ++left.version;
      right.alive = false;
      ++right.version;
      left.next = right.next;
      if (right.next != none) {
        nodes[right.next].previous = candidate.left;
      }
      enqueue(left.previous, candidates);
      enqueue(candidate.left, candidates);
    }
    std::vector<EncodedToken> result;
    result.reserve(nodes.size());
    std::size_t current = 0;
    while (current != none) {
      const Node& node = nodes[current];
      if (node.alive) result.push_back({node.id, input.substr(node.begin, node.length)});
      current = node.next;
    }
    return result;
  }

  const double negative_infinity = -std::numeric_limits<double>::infinity();
  std::vector<double> best(input.size() + 1, negative_infinity);
  std::vector<std::uint32_t> selected_id(input.size(), 0);
  std::vector<std::size_t> selected_end(input.size(), 0);
  best[input.size()] = 0.0;
  for (std::size_t reverse = input.size(); reverse > 0; --reverse) {
    const std::size_t begin = reverse - 1;
    std::size_t trie_node = 0;
    for (std::size_t end = begin; end < input.size(); ++end) {
      const auto byte = static_cast<unsigned char>(input[end]);
      const auto child = trie_[trie_node].children.find(byte);
      if (child == trie_[trie_node].children.end()) break;
      trie_node = child->second;
      if (trie_[trie_node].token_id && std::isfinite(best[end + 1])) {
        const std::uint32_t id = *trie_[trie_node].token_id;
        const double candidate = unigram_scores_[id] + best[end + 1];
        if (candidate > best[begin] ||
            (candidate == best[begin] && end + 1 > selected_end[begin])) {
          best[begin] = candidate;
          selected_id[begin] = id;
          selected_end[begin] = end + 1;
        }
      }
    }
    if (unknown_id_ && std::isfinite(best[begin + 1])) {
      const double candidate = unknown_score_ + best[begin + 1];
      if (candidate > best[begin]) {
        best[begin] = candidate;
        selected_id[begin] = *unknown_id_;
        selected_end[begin] = begin + 1;
      }
    }
  }
  if (!std::isfinite(best[0])) throw std::invalid_argument("Unigram vocabulary cannot encode the input");
  std::vector<EncodedToken> result;
  for (std::size_t begin = 0; begin < input.size();) {
    const std::size_t end = selected_end[begin];
    if (end <= begin) throw std::logic_error("Unigram tokenizer produced an invalid backpointer");
    result.push_back({selected_id[begin], input.substr(begin, end - begin)});
    begin = end;
  }
  return result;
}

std::string Tokenizer::decode(std::span<const std::uint32_t> token_ids) const {
  std::size_t size = 0;
  for (std::uint32_t id : token_ids) {
    if (id >= vocabulary_.size()) throw std::out_of_range("token ID is outside the vocabulary");
    if (vocabulary_[id].size() > std::numeric_limits<std::size_t>::max() - size) {
      throw std::length_error("decoded tokenizer output is too large");
    }
    size += vocabulary_[id].size();
  }
  std::string output;
  output.reserve(size);
  for (std::uint32_t id : token_ids) output += vocabulary_[id];
  return output;
}

} // namespace ax::ai
