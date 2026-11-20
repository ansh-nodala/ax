#include "runtime/core/physics.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <utility>

namespace ax::game {
namespace {

Vec3d cross(Vec3d a, Vec3d b) noexcept {
  return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

Vec3d minimum(Vec3d a, Vec3d b) noexcept {
  return {std::min(a.x, b.x), std::min(a.y, b.y), std::min(a.z, b.z)};
}

Vec3d maximum(Vec3d a, Vec3d b) noexcept {
  return {std::max(a.x, b.x), std::max(a.y, b.y), std::max(a.z, b.z)};
}

Vec3d centroid(const Triangle& triangle) noexcept {
  return (triangle.a + triangle.b + triangle.c) * (1.0 / 3.0);
}

bool intersect_bounds(Vec3d minimum_bound, Vec3d maximum_bound, const Ray& ray, double limit) {
  double near_distance = 0.0;
  double far_distance = limit;
  const double origins[3]{ray.origin.x, ray.origin.y, ray.origin.z};
  const double directions[3]{ray.direction.x, ray.direction.y, ray.direction.z};
  const double mins[3]{minimum_bound.x, minimum_bound.y, minimum_bound.z};
  const double maxs[3]{maximum_bound.x, maximum_bound.y, maximum_bound.z};
  for (unsigned axis = 0; axis < 3; ++axis) {
    if (std::abs(directions[axis]) < 1.0e-15) {
      if (origins[axis] < mins[axis] || origins[axis] > maxs[axis]) return false;
      continue;
    }
    const double inverse = 1.0 / directions[axis];
    double first = (mins[axis] - origins[axis]) * inverse;
    double last = (maxs[axis] - origins[axis]) * inverse;
    if (first > last) std::swap(first, last);
    near_distance = std::max(near_distance, first);
    far_distance = std::min(far_distance, last);
    if (near_distance > far_distance) return false;
  }
  return far_distance >= 0.0;
}

bool intersect_triangle(const Triangle& triangle, const Ray& ray, double limit,
                        double& distance, double& u, double& v) {
  constexpr double epsilon = 1.0e-12;
  const Vec3d edge1 = triangle.b - triangle.a;
  const Vec3d edge2 = triangle.c - triangle.a;
  const Vec3d p = cross(ray.direction, edge2);
  const double determinant = dot(edge1, p);
  if (std::abs(determinant) < epsilon) return false;
  const double inverse = 1.0 / determinant;
  const Vec3d offset = ray.origin - triangle.a;
  u = dot(offset, p) * inverse;
  if (u < 0.0 || u > 1.0) return false;
  const Vec3d q = cross(offset, edge1);
  v = dot(ray.direction, q) * inverse;
  if (v < 0.0 || u + v > 1.0) return false;
  distance = dot(edge2, q) * inverse;
  return distance >= 0.0 && distance <= limit;
}

} // namespace

double dot(Vec3d a, Vec3d b) noexcept { return a.x * b.x + a.y * b.y + a.z * b.z; }

SpringEnergy spring_energy_gradient(Vec3d a, Vec3d b, double rest_length, double stiffness) {
  if (!std::isfinite(rest_length) || !std::isfinite(stiffness) || rest_length < 0.0 || stiffness < 0.0) {
    throw std::invalid_argument("spring rest length and stiffness must be finite and nonnegative");
  }
  const Vec3d difference = a - b;
  const double length = std::sqrt(dot(difference, difference));
  const double extension = length - rest_length;
  const double energy = 0.5 * stiffness * extension * extension;
  Vec3d gradient{};
  if (length > 1.0e-15) gradient = difference * (stiffness * extension / length);
  return {energy, gradient, gradient * -1.0};
}

void TriangleBvh::build(std::vector<Triangle> triangles) {
  triangles_ = std::move(triangles);
  indices_.resize(triangles_.size());
  std::iota(indices_.begin(), indices_.end(), std::size_t{0});
  nodes_.clear();
  if (!triangles_.empty()) {
    if (triangles_.size() > std::numeric_limits<std::uint32_t>::max() / 2) {
      throw std::length_error("BVH exceeds 32-bit node indexing");
    }
    nodes_.reserve(triangles_.size() * 2);
    (void)build_node(0, triangles_.size());
  }
}

std::uint32_t TriangleBvh::build_node(std::size_t begin, std::size_t end) {
  const std::uint32_t node_index = static_cast<std::uint32_t>(nodes_.size());
  nodes_.push_back({});
  Vec3d lower{std::numeric_limits<double>::infinity(), std::numeric_limits<double>::infinity(),
              std::numeric_limits<double>::infinity()};
  Vec3d upper{-std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity(),
              -std::numeric_limits<double>::infinity()};
  Vec3d center_lower = lower;
  Vec3d center_upper = upper;
  for (std::size_t i = begin; i < end; ++i) {
    const Triangle& triangle = triangles_[indices_[i]];
    const Vec3d triangle_lower = minimum(triangle.a, minimum(triangle.b, triangle.c));
    const Vec3d triangle_upper = maximum(triangle.a, maximum(triangle.b, triangle.c));
    lower = minimum(lower, triangle_lower);
    upper = maximum(upper, triangle_upper);
    const Vec3d center = centroid(triangle);
    center_lower = minimum(center_lower, center);
    center_upper = maximum(center_upper, center);
  }
  nodes_[node_index].minimum = lower;
  nodes_[node_index].maximum = upper;
  const std::size_t count = end - begin;
  if (count <= 4) {
    nodes_[node_index].begin = begin;
    nodes_[node_index].count = count;
    return node_index;
  }

  const Vec3d extent = center_upper - center_lower;
  unsigned axis = extent.y > extent.x ? 1u : 0u;
  if ((axis == 0 ? extent.x : extent.y) < extent.z) axis = 2;
  const auto axis_value = [axis](Vec3d value) {
    return axis == 0 ? value.x : axis == 1 ? value.y : value.z;
  };
  const std::size_t middle = begin + count / 2;
  std::nth_element(indices_.begin() + static_cast<std::ptrdiff_t>(begin),
                   indices_.begin() + static_cast<std::ptrdiff_t>(middle),
                   indices_.begin() + static_cast<std::ptrdiff_t>(end),
                   [&](std::size_t left, std::size_t right) {
                     return axis_value(centroid(triangles_[left])) < axis_value(centroid(triangles_[right]));
                   });
  const std::uint32_t left = build_node(begin, middle);
  const std::uint32_t right = build_node(middle, end);
  nodes_[node_index].left = left;
  nodes_[node_index].right = right;
  return node_index;
}

bool TriangleBvh::intersect(const Ray& ray, RayHit& hit, double maximum_distance) const {
  if (nodes_.empty() || maximum_distance < 0.0) return false;
  std::vector<std::uint32_t> stack{0};
  double closest = maximum_distance;
  bool found = false;
  while (!stack.empty()) {
    const std::uint32_t node_index = stack.back();
    stack.pop_back();
    const Node& node = nodes_[node_index];
    if (!intersect_bounds(node.minimum, node.maximum, ray, closest)) continue;
    if (node.count != 0) {
      for (std::size_t i = node.begin; i < node.begin + node.count; ++i) {
        double distance = 0.0, u = 0.0, v = 0.0;
        const std::size_t triangle_index = indices_[i];
        if (intersect_triangle(triangles_[triangle_index], ray, closest, distance, u, v)) {
          closest = distance;
          hit = {triangle_index, distance, u, v};
          found = true;
        }
      }
    } else {
      stack.push_back(node.right);
      stack.push_back(node.left);
    }
  }
  return found;
}

} // namespace ax::game
