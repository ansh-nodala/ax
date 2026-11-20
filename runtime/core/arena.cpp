#include "runtime/core/axrt.hpp"
#include "runtime/core/arena.hpp"
extern "C" void* ax_arena_alloc(std::size_t n){ return ax::ax_arena_alloc_impl(n); }
extern "C" void ax_arena_reset(void){ ax::ax_arena_reset_impl(); }
