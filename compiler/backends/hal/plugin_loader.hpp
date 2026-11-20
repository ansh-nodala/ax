#pragma once
#include <string>
namespace ax { void* load_plugin(const std::string& path); void* plugin_symbol(void* h, const std::string& name); }
