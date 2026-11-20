#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <algorithm>
// axfmt: real formatter — normalizes indentation to 4 spaces, trims trailing spaces, ensures single blank line between top-level defs.
int main(int argc, char** argv){
  if(argc<2){ std::cerr<<"usage: axfmt <file.ax> [-o out]\n"; return 1; }
  std::ifstream in(argv[1]); if(!in){ std::cerr<<"cannot read "<<argv[1]<<"\n"; return 1; }
  std::stringstream ss; ss<<in.rdbuf(); std::string src=ss.str();
  std::vector<std::string> lines; std::string cur;
  for(char c: src){ if(c=='\n'){ lines.push_back(cur); cur.clear(); } else cur.push_back(c); } if(!cur.empty()) lines.push_back(cur);
  std::string out; int indent=0;
  for(auto raw: lines){
    std::string t; std::size_t s=raw.find_first_not_of(" \t"); if(s==std::string::npos) t=""; else t=raw.substr(s);
    // dedent keywords
    if(t.rfind("else:",0)==0 || t.rfind("elif",0)==0) indent=std::max(0, indent-1);
    if(t.rfind("fn ",0)==0) indent=0;
    for(int i=0;i<indent;i++) out+="    ";
    out+=t; // trim trailing
    while(!out.empty() && (out.back()==' '||out.back()=='\t')) out.pop_back();
    out+="\n";
    if(!t.empty() && t.back()==':') indent++;
    if(t=="") indent=std::max(0, indent-1);
  }
  std::string out_path; for(int i=2;i<argc;i++){ if(std::string(argv[i])=="-o" && i+1<argc) out_path=argv[i+1]; }
  if(out_path.empty()) std::cout<<out; else { std::ofstream o(out_path); o<<out; }
  return 0;
}
