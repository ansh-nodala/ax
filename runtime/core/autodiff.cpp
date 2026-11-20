#include "runtime/core/autodiff.hpp"

#include <cmath>
#include <stdexcept>
#include <utility>

namespace ax::ai {
namespace {

double unary_first(DiffGraph::Op op, double x, double y) {
  switch (op) {
    case DiffGraph::Op::Sin: return std::cos(x);
    case DiffGraph::Op::Cos: return -std::sin(x);
    case DiffGraph::Op::Exp: return y;
    case DiffGraph::Op::Log: return 1.0 / x;
    case DiffGraph::Op::Tanh: return 1.0 - y * y;
    case DiffGraph::Op::Relu: return x > 0.0 ? 1.0 : 0.0;
    default: throw std::logic_error("requested derivative for a non-unary operation");
  }
}

double unary_second(DiffGraph::Op op, double x, double y) {
  switch (op) {
    case DiffGraph::Op::Sin: return -std::sin(x);
    case DiffGraph::Op::Cos: return -std::cos(x);
    case DiffGraph::Op::Exp: return y;
    case DiffGraph::Op::Log: return -1.0 / (x * x);
    case DiffGraph::Op::Tanh: {
      const double first = 1.0 - y * y;
      return -2.0 * y * first;
    }
    case DiffGraph::Op::Relu: return 0.0;
    default: throw std::logic_error("requested second derivative for a non-unary operation");
  }
}

double eval_unary(DiffGraph::Op op, double x) {
  switch (op) {
    case DiffGraph::Op::Sin: return std::sin(x);
    case DiffGraph::Op::Cos: return std::cos(x);
    case DiffGraph::Op::Exp: return std::exp(x);
    case DiffGraph::Op::Log:
      if (x <= 0.0) throw std::domain_error("log autodiff input must be positive");
      return std::log(x);
    case DiffGraph::Op::Tanh: return std::tanh(x);
    case DiffGraph::Op::Relu: return x > 0.0 ? x : 0.0;
    default: throw std::logic_error("requested value for a non-unary operation");
  }
}

struct BinaryPartials {
  double lhs;
  double rhs;
  double lhs_lhs;
  double lhs_rhs;
  double rhs_rhs;
};

BinaryPartials binary_partials(DiffGraph::Op op, double lhs, double rhs) {
  switch (op) {
    case DiffGraph::Op::Add: return {1.0, 1.0, 0.0, 0.0, 0.0};
    case DiffGraph::Op::Subtract: return {1.0, -1.0, 0.0, 0.0, 0.0};
    case DiffGraph::Op::Multiply: return {rhs, lhs, 0.0, 1.0, 0.0};
    case DiffGraph::Op::Divide:
      if (rhs == 0.0) throw std::domain_error("division by zero in autodiff graph");
      return {1.0 / rhs, -lhs / (rhs * rhs), 0.0,
              -1.0 / (rhs * rhs), 2.0 * lhs / (rhs * rhs * rhs)};
    default: throw std::logic_error("requested partials for a non-binary operation");
  }
}

double eval_binary(DiffGraph::Op op, double lhs, double rhs) {
  switch (op) {
    case DiffGraph::Op::Add: return lhs + rhs;
    case DiffGraph::Op::Subtract: return lhs - rhs;
    case DiffGraph::Op::Multiply: return lhs * rhs;
    case DiffGraph::Op::Divide:
      if (rhs == 0.0) throw std::domain_error("division by zero in autodiff graph");
      return lhs / rhs;
    default: throw std::logic_error("requested value for a non-binary operation");
  }
}

} // namespace

DiffNode DiffGraph::append(Node node) {
  nodes_.push_back(std::move(node));
  return nodes_.size() - 1;
}

void DiffGraph::require_node(DiffNode node) const {
  if (node >= nodes_.size()) throw std::out_of_range("autodiff node does not belong to this graph");
}

DiffNode DiffGraph::input(std::size_t index) {
  if (index >= input_count_) throw std::out_of_range("autodiff input index is out of range");
  Node node{};
  node.op = Op::Input;
  node.input_index = index;
  return append(node);
}

DiffNode DiffGraph::constant(double value) {
  Node node{};
  node.op = Op::Constant;
  node.value = value;
  return append(node);
}

DiffNode DiffGraph::add(DiffNode lhs, DiffNode rhs) {
  require_node(lhs); require_node(rhs);
  return append(Node{Op::Add, lhs, rhs});
}
DiffNode DiffGraph::subtract(DiffNode lhs, DiffNode rhs) {
  require_node(lhs); require_node(rhs);
  return append(Node{Op::Subtract, lhs, rhs});
}
DiffNode DiffGraph::multiply(DiffNode lhs, DiffNode rhs) {
  require_node(lhs); require_node(rhs);
  return append(Node{Op::Multiply, lhs, rhs});
}
DiffNode DiffGraph::divide(DiffNode lhs, DiffNode rhs) {
  require_node(lhs); require_node(rhs);
  return append(Node{Op::Divide, lhs, rhs});
}

#define AX_DIFF_UNARY(method, operation) \
  DiffNode DiffGraph::method(DiffNode value) { \
    require_node(value); \
    Node node{}; node.op = Op::operation; node.lhs = value; \
    return append(node); \
  }
AX_DIFF_UNARY(sin, Sin)
AX_DIFF_UNARY(cos, Cos)
AX_DIFF_UNARY(exp, Exp)
AX_DIFF_UNARY(log, Log)
AX_DIFF_UNARY(tanh, Tanh)
AX_DIFF_UNARY(relu, Relu)
#undef AX_DIFF_UNARY

std::vector<double> DiffGraph::evaluate_nodes(const std::vector<double>& inputs) const {
  if (inputs.size() != input_count_) throw std::invalid_argument("autodiff input count mismatch");
  std::vector<double> values(nodes_.size());
  for (std::size_t i = 0; i < nodes_.size(); ++i) {
    const Node& node = nodes_[i];
    switch (node.op) {
      case Op::Input: values[i] = inputs[node.input_index]; break;
      case Op::Constant: values[i] = node.value; break;
      case Op::Add: case Op::Subtract: case Op::Multiply: case Op::Divide:
        values[i] = eval_binary(node.op, values[node.lhs], values[node.rhs]); break;
      default: values[i] = eval_unary(node.op, values[node.lhs]); break;
    }
  }
  return values;
}

std::vector<double> DiffGraph::evaluate(const std::vector<double>& inputs,
                                        const std::vector<DiffNode>& outputs) const {
  const auto values = evaluate_nodes(inputs);
  std::vector<double> result;
  result.reserve(outputs.size());
  for (DiffNode output : outputs) {
    require_node(output);
    result.push_back(values[output]);
  }
  return result;
}

std::vector<double> DiffGraph::vjp(const std::vector<double>& inputs, DiffNode output,
                                   double seed) const {
  require_node(output);
  const auto values = evaluate_nodes(inputs);
  std::vector<double> adjoints(nodes_.size(), 0.0);
  std::vector<double> result(input_count_, 0.0);
  adjoints[output] = seed;
  for (std::size_t i = nodes_.size(); i-- > 0;) {
    const Node& node = nodes_[i];
    const double adjoint = adjoints[i];
    switch (node.op) {
      case Op::Input: result[node.input_index] += adjoint; break;
      case Op::Constant: break;
      case Op::Add: case Op::Subtract: case Op::Multiply: case Op::Divide: {
        const auto partials = binary_partials(node.op, values[node.lhs], values[node.rhs]);
        adjoints[node.lhs] += adjoint * partials.lhs;
        adjoints[node.rhs] += adjoint * partials.rhs;
        break;
      }
      default:
        adjoints[node.lhs] += adjoint * unary_first(node.op, values[node.lhs], values[i]);
        break;
    }
  }
  return result;
}

double DiffGraph::jvp(const std::vector<double>& inputs, DiffNode output,
                      const std::vector<double>& tangent) const {
  require_node(output);
  if (tangent.size() != input_count_) throw std::invalid_argument("autodiff tangent count mismatch");
  const auto values = evaluate_nodes(inputs);
  std::vector<double> tangents(nodes_.size(), 0.0);
  for (std::size_t i = 0; i < nodes_.size(); ++i) {
    const Node& node = nodes_[i];
    switch (node.op) {
      case Op::Input: tangents[i] = tangent[node.input_index]; break;
      case Op::Constant: break;
      case Op::Add: case Op::Subtract: case Op::Multiply: case Op::Divide: {
        const auto partials = binary_partials(node.op, values[node.lhs], values[node.rhs]);
        tangents[i] = partials.lhs * tangents[node.lhs] + partials.rhs * tangents[node.rhs];
        break;
      }
      default:
        tangents[i] = unary_first(node.op, values[node.lhs], values[i]) * tangents[node.lhs];
        break;
    }
  }
  return tangents[output];
}

std::vector<double> DiffGraph::jacobian(const std::vector<double>& inputs,
                                        const std::vector<DiffNode>& outputs) const {
  std::vector<double> result;
  result.reserve(outputs.size() * input_count_);
  for (DiffNode output : outputs) {
    const auto row = vjp(inputs, output);
    result.insert(result.end(), row.begin(), row.end());
  }
  return result;
}

std::vector<double> DiffGraph::hessian(const std::vector<double>& inputs,
                                       DiffNode output) const {
  require_node(output);
  if (inputs.size() != input_count_) throw std::invalid_argument("autodiff input count mismatch");
  struct Jet {
    double value = 0.0;
    std::vector<double> gradient;
    std::vector<double> hessian;
  };
  const std::size_t n = input_count_;
  std::vector<Jet> jets(nodes_.size());
  for (std::size_t i = 0; i < nodes_.size(); ++i) {
    const Node& node = nodes_[i];
    Jet& current = jets[i];
    current.gradient.assign(n, 0.0);
    current.hessian.assign(n * n, 0.0);
    if (node.op == Op::Input) {
      current.value = inputs[node.input_index];
      current.gradient[node.input_index] = 1.0;
      continue;
    }
    if (node.op == Op::Constant) {
      current.value = node.value;
      continue;
    }
    if (node.op == Op::Add || node.op == Op::Subtract ||
        node.op == Op::Multiply || node.op == Op::Divide) {
      const Jet& lhs = jets[node.lhs];
      const Jet& rhs = jets[node.rhs];
      const auto p = binary_partials(node.op, lhs.value, rhs.value);
      current.value = eval_binary(node.op, lhs.value, rhs.value);
      for (std::size_t a = 0; a < n; ++a) {
        current.gradient[a] = p.lhs * lhs.gradient[a] + p.rhs * rhs.gradient[a];
        for (std::size_t b = 0; b < n; ++b) {
          const std::size_t index = a * n + b;
          current.hessian[index] = p.lhs * lhs.hessian[index] + p.rhs * rhs.hessian[index] +
              p.lhs_lhs * lhs.gradient[a] * lhs.gradient[b] +
              p.lhs_rhs * (lhs.gradient[a] * rhs.gradient[b] + rhs.gradient[a] * lhs.gradient[b]) +
              p.rhs_rhs * rhs.gradient[a] * rhs.gradient[b];
        }
      }
    } else {
      const Jet& source = jets[node.lhs];
      current.value = eval_unary(node.op, source.value);
      const double first = unary_first(node.op, source.value, current.value);
      const double second = unary_second(node.op, source.value, current.value);
      for (std::size_t a = 0; a < n; ++a) {
        current.gradient[a] = first * source.gradient[a];
        for (std::size_t b = 0; b < n; ++b) {
          const std::size_t index = a * n + b;
          current.hessian[index] = first * source.hessian[index] +
              second * source.gradient[a] * source.gradient[b];
        }
      }
    }
  }
  return jets[output].hessian;
}

} // namespace ax::ai
