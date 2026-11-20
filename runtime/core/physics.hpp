#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace ax::game {

struct Vec3d {
  double x = 0.0;
  double y = 0.0;
  double z = 0.0;
  friend Vec3d operator+(Vec3d a, Vec3d b) noexcept { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
  friend Vec3d operator-(Vec3d a, Vec3d b) noexcept { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
  friend Vec3d operator*(Vec3d a, double s) noexcept { return {a.x * s, a.y * s, a.z * s}; }
};

double dot(Vec3d a, Vec3d b) noexcept;

struct SpringEnergy {
  double energy;
  Vec3d gradient_a;
  Vec3d gradient_b;
};

SpringEnergy spring_energy_gradient(Vec3d a, Vec3d b, double rest_length, double stiffness);

struct Triangle {
  Vec3d a;
  Vec3d b;
  Vec3d c;
};

struct Ray {
  Vec3d origin;
  Vec3d direction;
};

struct RayHit {
  std::size_t triangle_index;
  double distance;
  double barycentric_u;
  double barycentric_v;
};

class TriangleBvh {
 public:
  void build(std::vector<Triangle> triangles);
  bool intersect(const Ray& ray, RayHit& hit, double maximum_distance = 1.0e30) const;
  std::size_t triangle_count() const noexcept { return triangles_.size(); }
  std::size_t node_count() const noexcept { return nodes_.size(); }

 private:
  struct Node {
    Vec3d minimum;
    Vec3d maximum;
    std::uint32_t left = 0;
    std::uint32_t right = 0;
    std::size_t begin = 0;
    std::size_t count = 0;
  };

  std::uint32_t build_node(std::size_t begin, std::size_t end);
  std::vector<Triangle> triangles_;
  std::vector<std::size_t> indices_;
  std::vector<Node> nodes_;
};

} // namespace ax::game
