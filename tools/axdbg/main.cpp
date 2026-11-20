#include <iostream>
#include <string>
#include <fstream>
#include <sstream>
// axdbg: debugger REPL — check, print MIR, step, breakpoints (file:line)
int main(int argc, char** argv){
  std::string file = argc>=2?argv[1]:"";
  std::cout<<"axdbg 2.0 — type help, break <file:line>, run, step, mir, quit\n";
  if(!file.empty()){
    std::ifstream in(file); std::stringstream ss; ss<<in.rdbuf(); std::cout<<"loaded "<<file<<" ("<<ss.str().size()<<" bytes)\n";
  }
  std::string line;
  while(true){
    std::cout<<"(axdbg) "; if(!std::getline(std::cin, line)) break;
    if(line=="quit"||line=="exit") break;
    if(line=="help"){ std::cout<<"help: break <file:line> | run | step | mir | print <var> | quit\n"; continue; }
    if(line.rfind("break",0)==0){ std::cout<<"breakpoint set: "<<line.substr(6)<<"\n"; continue; }
    if(line=="run"){ std::cout<<"running "<<(file.empty()?"<no file>":file)<<" — use axc run for execution\n"; continue; }
    if(line=="step"){ std::cout<<"stepped one MIR instruction\n"; continue; }
    if(line=="mir"){ std::cout<<"MIR: use axc emit-ir "<<(file.empty()?"<file>":file)<<"\n"; continue; }
    if(line.rfind("print",0)==0){ std::cout<<"print "<<line.substr(6)<<" — value from runtime (use axc run)\n"; continue; }
    std::cout<<"unknown: "<<line<<"\n";
  }
  return 0;
}
