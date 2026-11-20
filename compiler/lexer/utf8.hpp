#pragma once
#include <string>
#include <cstdint>
namespace ax {
// Decode one UTF-8 codepoint at pos, return codepoint and advance pos.
inline bool utf8_decode(const std::string& s, std::size_t& pos, uint32_t& cp) {
  unsigned char c = (unsigned char)s[pos];
  if(c < 0x80){ cp=c; pos+=1; return true; }
  if((c & 0xE0)==0xC0 && pos+1<s.size()){ cp=((c&0x1F)<<6)|((unsigned char)s[pos+1]&0x3F); pos+=2; return true; }
  if((c & 0xF0)==0xE0 && pos+2<s.size()){ cp=((c&0x0F)<<12)|(((unsigned char)s[pos+1]&0x3F)<<6)|((unsigned char)s[pos+2]&0x3F); pos+=3; return true; }
  if((c & 0xF8)==0xF0 && pos+3<s.size()){ cp=((c&0x07)<<18)|(((unsigned char)s[pos+1]&0x3F)<<12)|(((unsigned char)s[pos+2]&0x3F)<<6)|((unsigned char)s[pos+3]&0x3F); pos+=4; return true; }
  pos+=1; cp=c; return true;
}
// Map math codepoints to ASCII operators / names.
inline bool math_cp_to_op(uint32_t cp, std::string& out) {
  if(cp==0x00D7){ out="*"; return true; } // ×
  if(cp==0x00F7){ out="/"; return true; } // ÷
  if(cp==0x2264){ out="<="; return true; } // ≤
  if(cp==0x2265){ out=">="; return true; } // ≥
  if(cp==0x2260){ out="!="; return true; } // ≠
  if(cp==0x03C0){ out="3.141592653589793"; return true; } // π as number text
  return false;
}
} // namespace ax
