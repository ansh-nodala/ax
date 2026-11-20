#pragma once
#include <cstdint>
#include <vector>
namespace ax {
// AArch64 instruction encoders (subset, AAPCS64). Encodings follow ARM ARM.
inline uint32_t enc_mov_imm(int rd, int imm16){ // MOVZ Rd, #imm
  return (uint32_t)(0xD2800000u | ((imm16 & 0xFFFFu)<<5) | (rd & 0x1F));
}
inline uint32_t enc_add_imm(int rd, int rn, int imm12){ return (uint32_t)(0x91000000u | ((imm12&0xFFFu)<<10) | ((rn&0x1F)<<5) | (rd&0x1F)); }
inline uint32_t enc_sub_imm(int rd, int rn, int imm12){ return (uint32_t)(0xD1000000u | ((imm12&0xFFFu)<<10) | ((rn&0x1F)<<5) | (rd&0x1F)); }
inline uint32_t enc_ret(){ return 0xD65F03C0u; }
inline uint32_t enc_nop(){ return 0xD503201Fu; }
inline uint32_t enc_fadd_s(int rd, int rn, int rm){ return (uint32_t)(0x1E222800u | ((rm&0x1F)<<16) | ((rn&0x1F)<<5) | (rd&0x1F)); }
inline uint32_t enc_fmul_s(int rd, int rn, int rm){ return (uint32_t)(0x1E220800u | ((rm&0x1F)<<16) | ((rn&0x1F)<<5) | (rd&0x1F)); }
} // namespace ax
