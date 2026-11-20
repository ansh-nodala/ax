#pragma once
#include "runtime/core/axrt.hpp"
namespace ax { inline AxVal* gemm(AxVal* a, AxVal* b){ return ax_gemm(a,b); } }
