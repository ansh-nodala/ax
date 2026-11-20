#include "compiler/backends/vulkan/spirv_backend.hpp"
#include <fstream>
namespace ax {
static void emit_inst(std::vector<uint32_t>& out, uint16_t op, const std::vector<uint32_t>& words){
  uint16_t wc = (uint16_t)(1 + words.size());
  out.push_back(((uint32_t)wc<<16) | op);
  for(auto w: words) out.push_back(w);
}
std::vector<uint32_t> emit_spirv_binary(const std::string& fn_name){
  (void)fn_name;
  std::vector<uint32_t> w;
  w.push_back(0x07230203u); // magic
  w.push_back(0x00010500u); // version 1.5
  w.push_back(0x000A0000u); // generator AX
  w.push_back(32); // bound
  w.push_back(0); // schema
  // OpCapability Shader (2,0)
  emit_inst(w, 17, {1});
  // OpMemoryModel Logical GLSL450 (3,0,1)
  emit_inst(w, 14, {0, 1});
  // OpEntryPoint GLCompute (5, func=1, name, interface)
  // name "main" packed as words
  uint32_t name_words[2] = {0x6E69616Du, 0}; // "main\0"
  emit_inst(w, 15, {5, 1, name_words[0], name_words[1], 8, 9});
  // OpExecutionMode LocalSize (6, func, 17, 64,1,1)
  emit_inst(w, 16, {1, 17, 64, 1, 1});
  // OpSource GLSL (3, 450)
  emit_inst(w, 3, {1, 450});
  // OpName (5, target, "main")
  emit_inst(w, 5, {1, name_words[0], name_words[1]});
  // OpDecorate (71, target 8, DescriptorSet 0, Binding 0) -> two decorates
  emit_inst(w, 71, {8, 0, 0});
  emit_inst(w, 71, {8, 1, 0});
  emit_inst(w, 71, {9, 0, 1});
  emit_inst(w, 71, {9, 1, 0});
  // Types: 2=void, 3=bool, 4=int, 5=float, 6=vec, 7=ptr, 10=function, 11=struct runtime array
  emit_inst(w, 19, {2}); // OpTypeVoid
  emit_inst(w, 21, {3, 32, 1}); // OpTypeInt 32 signed
  emit_inst(w, 22, {4, 32}); // OpTypeFloat 32
  emit_inst(w, 23, {5, 4, 1}); // OpTypeVector float x1? use float vector
  emit_inst(w, 23, {6, 4, 4}); // vec4
  emit_inst(w, 28, {7, 2}); // OpTypePointer StorageBuffer void? simplified
  emit_inst(w, 29, {8, 4}); // OpTypeRuntimeArray float
  emit_inst(w, 30, {9, 8}); // OpTypeStruct runtime array
  emit_inst(w, 32, {10, 7, 2}); // OpTypePointer StorageBuffer struct
  emit_inst(w, 33, {11, 9, 10}); // OpTypeFunction void func
  // Variables
  emit_inst(w, 59, {10, 8, 1}); // OpVariable ptr StorageBuffer (id 8)
  emit_inst(w, 59, {10, 9, 1}); // OpVariable id 9
  // Function
  emit_inst(w, 54, {2, 1, 0, 11}); // OpFunction void, id 1, None, type 11
  emit_inst(w, 248, {12}); // OpLabel 12
  // OpLoad / OpStore simplified: load float, fadd, store
  emit_inst(w, 61, {4, 13, 8}); // OpLoad float id13 from ptr 8
  emit_inst(w, 62, {4, 14, 13}); // intermediate value
  emit_inst(w, 129, {4, 15, 13, 14}); // OpFAdd (129) id15 = id13+id14
  emit_inst(w, 62, {9, 15}); // OpStore (62) to 9 value 15
  emit_inst(w, 253, {}); // OpReturn
  emit_inst(w, 56, {}); // OpFunctionEnd
  return w;
}
bool write_spirv_file(const std::string& path, const std::vector<uint32_t>& words){
  std::ofstream out(path, std::ios::binary); if(!out) return false;
  for(auto w: words) out.write((char*)&w, 4);
  return true;
}
bool validate_spirv(const std::vector<uint32_t>& words, std::string& err){
  if(words.size()<5){ err="too short"; return false; }
  if(words[0]!=0x07230203u){ err="bad magic"; return false; }
  if(words[3]==0){ err="bound is zero"; return false; }
  std::size_t i=5;
  while(i<words.size()){
    uint32_t first=words[i]; uint16_t wc=(uint16_t)(first>>16); uint16_t op=(uint16_t)(first&0xFFFF);
    if(wc==0){ err="zero word count"; return false; }
    if(i+wc>words.size()){ err="truncated instruction"; return false; }
    (void)op; i+=wc;
  }
  if(i!=words.size()){ err="trailing words"; return false; }
  return true;
}
std::string emit_vulkan_host(const std::string& fn_name){
  (void)fn_name;
  return "// Vulkan compute dispatch via dlopen(libvulkan.so): vkCreateInstance, vkCreateDevice, vkCreateComputePipelines, vkQueueSubmit\n";
}
} // namespace ax
