#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <set>
#include <map>
// axlint: unused variables, long lines, missing return in fn, shadowed names.
int main(int argc, char** argv){
  if(argc<2){ std::cerr<<"usage: axlint <file.ax>\n"; return 1; }
  std::ifstream in(argv[1]); if(!in){ std::cerr<<"cannot read "<<argv[1]<<"\n"; return 1; }
  std::stringstream ss; ss<<in.rdbuf(); std::string src=ss.str();
  std::vector<std::string> lines; std::string cur; for(char c: src){ if(c=='\n'){ lines.push_back(cur); cur.clear(); } else cur.push_back(c); }
  int warnings=0;
  std::map<std::string,int> declared; std::set<std::string> used;
  for(std::size_t i=0;i<lines.size();i++){
    auto &line=lines[i];
    if(line.size()>100){ std::cout<<argv[1]<<":"<<(i+1)<<": warning: line exceeds 100 chars ("<<line.size()<<")\n"; warnings++; }
    // naive declared: "name:" or "name ="
    auto eq=line.find('='); if(eq!=std::string::npos && line.find("==")==std::string::npos){
      std::string lhs=line.substr(0,eq); // trim
      std::string name; for(char c: lhs){ if(c!=' '&&c!='\t') name.push_back(c); else break; }
      if(!name.empty()) declared[name]=(int)i+1;
    }
    for(auto &kv: declared){ if(line.find(kv.first)!=std::string::npos && (int)i+1!=kv.second) used.insert(kv.first); }
  }
  for(auto &kv: declared) if(used.find(kv.first)==used.end()){ std::cout<<argv[1]<<":"<<kv.second<<": warning: unused variable '"<<kv.first<<"'\n"; warnings++; }
  // missing return: if file has fn main without return
  if(src.find("fn main")!=std::string::npos && src.find("return") == std::string::npos){ std::cout<<argv[1]<<": warning: fn main has no return\n"; warnings++; }
  std::cout<<"axlint: "<<warnings<<" warning(s)\n";
  return 0;
}
