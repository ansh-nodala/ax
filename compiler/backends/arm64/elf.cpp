#include "compiler/backends/arm64/elf.hpp"
#include <fstream>
#include <cstring>
namespace ax {
struct Elf64_Ehdr { unsigned char e_ident[16]; uint16_t e_type; uint16_t e_machine; uint32_t e_version; uint64_t e_entry; uint64_t e_phoff; uint64_t e_shoff; uint32_t e_flags; uint16_t e_ehsize; uint16_t e_phentsize; uint16_t e_phnum; uint16_t e_shentsize; uint16_t e_shnum; uint16_t e_shstrndx; };
struct Elf64_Phdr { uint32_t p_type; uint32_t p_flags; uint64_t p_offset; uint64_t p_vaddr; uint64_t p_paddr; uint64_t p_filesz; uint64_t p_memsz; uint64_t p_align; };
static void write_hdr(std::ofstream& out, bool exec, uint64_t entry, uint64_t phoff){
  Elf64_Ehdr h{}; h.e_ident[0]=0x7F; h.e_ident[1]='E'; h.e_ident[2]='L'; h.e_ident[3]='F'; h.e_ident[4]=2; h.e_ident[5]=1; h.e_ident[6]=1; h.e_machine=183; h.e_version=1; h.e_type=exec?2:1; h.e_entry=entry; h.e_phoff=phoff; h.e_shoff=0; h.e_flags=0; h.e_ehsize=64; h.e_phentsize=56; h.e_phnum=exec?1:0; h.e_shentsize=0; h.e_shnum=0; h.e_shstrndx=0;
  out.write((char*)&h, sizeof(h));
}
bool write_elf64_object(const std::string& path, const std::vector<uint32_t>& code){
  std::ofstream out(path, std::ios::binary); if(!out) return false;
  write_hdr(out, false, 0, 0);
  // .text bytes follow header (simplified object: header + code, no section headers for brevity but valid magic/machine)
  for(auto w: code) out.write((char*)&w, 4);
  return true;
}
bool write_elf64_executable(const std::string& path, const std::vector<uint32_t>& code){
  std::ofstream out(path, std::ios::binary); if(!out) return false;
  uint64_t base=0x400000; uint64_t entry=base+64+56;
  write_hdr(out, true, entry, 64);
  Elf64_Phdr ph{}; ph.p_type=1; ph.p_flags=5; ph.p_offset=0; ph.p_vaddr=base; ph.p_paddr=base; ph.p_filesz=64+56+code.size()*4; ph.p_memsz=ph.p_filesz; ph.p_align=0x1000;
  out.write((char*)&ph, sizeof(ph));
  for(auto w: code) out.write((char*)&w, 4);
  return true;
}
} // namespace ax
