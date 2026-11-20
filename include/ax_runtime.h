#ifndef AX_RUNTIME_H
#define AX_RUNTIME_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct AxVal { int kind; double d; double* arr; int len; char* s; } AxVal;

AxVal* ax_scalar(double value);
AxVal* ax_string(const char* value);
AxVal* ax_tensor_vals(double* values, int count);
AxVal* ax_vec4(AxVal* a, AxVal* b, AxVal* c, AxVal* d);
AxVal* ax_len(AxVal* value);
AxVal* ax_add(AxVal* a, AxVal* b);
AxVal* ax_sub(AxVal* a, AxVal* b);
AxVal* ax_mul(AxVal* a, AxVal* b);
AxVal* ax_div(AxVal* a, AxVal* b);
AxVal* ax_mod(AxVal* a, AxVal* b);
AxVal* ax_neg(AxVal* value);
AxVal* ax_eq(AxVal* a, AxVal* b);
AxVal* ax_ne(AxVal* a, AxVal* b);
AxVal* ax_lt(AxVal* a, AxVal* b);
AxVal* ax_gt(AxVal* a, AxVal* b);
AxVal* ax_le(AxVal* a, AxVal* b);
AxVal* ax_ge(AxVal* a, AxVal* b);
void ax_print(AxVal* value);
int ax_truthy(AxVal* value);
double ax_to_double(AxVal* value);
AxVal* ax_matmul(AxVal* a, AxVal* b);
AxVal* ax_softmax(AxVal* value);
AxVal* ax_relu(AxVal* value);
AxVal* ax_layernorm(AxVal* value);
AxVal* ax_sigmoid(AxVal* value);
AxVal* ax_tanh(AxVal* value);
AxVal* ax_gemm(AxVal* a, AxVal* b);
AxVal* ax_rmsnorm(AxVal* value);
AxVal* ax_rope(AxVal* value);
AxVal* ax_rope_offset(AxVal* value, AxVal* position_offset);
AxVal* ax_rope_ex(AxVal* value, AxVal* sequence_length,
                  AxVal* head_dimension, AxVal* position_offset);
AxVal* ax_swiglu(AxVal* value);
AxVal* ax_swiglu2(AxVal* gate, AxVal* up);
AxVal* ax_geglu_self(AxVal* value);
AxVal* ax_geglu(AxVal* gate, AxVal* up);
AxVal* ax_gelu(AxVal* value);
AxVal* ax_silu(AxVal* value);
AxVal* ax_mse_loss(AxVal* prediction, AxVal* target);
AxVal* ax_cross_entropy(AxVal* logits, AxVal* labels);
AxVal* ax_adamw(AxVal* parameters, AxVal* gradients, double learning_rate);
typedef struct AxGpuKernelConfig {
  uint32_t kind;
  uint32_t input_count;
  uint32_t output_count;
  uint32_t rows;
  uint32_t columns;
  uint32_t inner;
  uint32_t local_x;
  uint32_t local_y;
} AxGpuKernelConfig;
AxVal* ax_vulkan_run_spirv(const uint32_t* spirv, size_t word_count,
                           AxVal* const* inputs, size_t input_count,
                           const AxGpuKernelConfig* config);
void* ax_arena_alloc(size_t size);
void ax_arena_reset(void);
int ax_topology_cores(void);
int ax_topology_has_neon(void);
int ax_topology_has_sve(void);
int ax_topology_has_fp16(void);
int ax_topology_has_bf16(void);
int ax_topology_has_dotprod(void);
double ax_math_sqrt(double value);
double ax_math_exp(double value);
double ax_math_log(double value);
double ax_math_sin(double value);
double ax_math_cos(double value);

#ifdef __cplusplus
}
#endif

#endif
