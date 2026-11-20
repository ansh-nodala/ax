#pragma once
#include "runtime/dist/dist.hpp"
namespace ax { inline bool allreduce(std::vector<double>& d, int r, int w){ return ax_dist_ring_allreduce(d,r,w,"127.0.0.1", 0); } }
