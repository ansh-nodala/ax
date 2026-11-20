#pragma once

#include <cstddef>
#include <vector>

namespace ax::ai {

using DiffNode = std::size_t;

class DiffGraph {
 public:
  enum class Op { Input, Constant, Add, Subtract, Multiply, Divide,
                  Sin, Cos, Exp, Log, Tanh, Relu };

  explicit DiffGraph(std::size_t input_count) : input_count_(input_count) {}

  DiffNode input(std::size_t index);
  DiffNode constant(double value);
  DiffNode add(DiffNode lhs, DiffNode rhs);
  DiffNode subtract(DiffNode lhs, DiffNode rhs);
  DiffNode multiply(DiffNode lhs, DiffNode rhs);
  DiffNode divide(DiffNode lhs, DiffNode rhs);
  DiffNode sin(DiffNode value);
  DiffNode cos(DiffNode value);
  DiffNode exp(DiffNode value);
  DiffNode log(DiffNode value);
  DiffNode tanh(DiffNode value);
  DiffNode relu(DiffNode value);

  std::vector<double> evaluate(const std::vector<double>& inputs,
                               const std::vector<DiffNode>& outputs) const;
  std::vector<double> vjp(const std::vector<double>& inputs, DiffNode output,
                          double seed = 1.0) const;
  double jvp(const std::vector<double>& inputs, DiffNode output,
             const std::vector<double>& tangent) const;
  // Jacobian is flattened in row-major output-by-input order.
  std::vector<double> jacobian(const std::vector<double>& inputs,
                               const std::vector<DiffNode>& outputs) const;
  // Hessian is flattened in row-major input-by-input order.
  std::vector<double> hessian(const std::vector<double>& inputs,
                              DiffNode output) const;

  std::size_t input_count() const noexcept { return input_count_; }
  std::size_t node_count() const noexcept { return nodes_.size(); }

 private:
  struct Node {
    Op op;
    DiffNode lhs = 0;
    DiffNode rhs = 0;
    std::size_t input_index = 0;
    double value = 0.0;
  };

  DiffNode append(Node node);
  void require_node(DiffNode node) const;
  std::vector<double> evaluate_nodes(const std::vector<double>& inputs) const;

  std::size_t input_count_;
  std::vector<Node> nodes_;
};

} // namespace ax::ai
