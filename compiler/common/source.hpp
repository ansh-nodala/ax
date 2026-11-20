#pragma once
#include <string>
#include <fstream>
#include <sstream>
namespace ax {
inline bool read_file(const std::string& path, std::string& out) {
  std::ifstream in(path); if(!in) return false; std::stringstream ss; ss<<in.rdbuf(); out=ss.str(); return true;
}
inline bool write_file(const std::string& path, const std::string& content) {
  std::ofstream out(path, std::ios::binary); if(!out) return false; out<<content; return true;
}
} // namespace ax
