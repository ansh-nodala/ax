#include <iostream>
#include <string>
#include <sstream>
// axlsp: JSON-RPC 2.0 over stdio — initialize, hover, completion, diagnostics.
static std::string read_msg(){
  std::string line; int len=-1;
  while(std::getline(std::cin, line)){
    if(line.rfind("Content-Length:",0)==0){ len=std::stoi(line.substr(15)); }
    if(line=="\r" || line.empty()) break;
  }
  if(len<=0) return "";
  std::string body(len, 0); std::cin.read(body.data(), len);
  // consume trailing newline if present
  return body;
}
static void send_msg(const std::string& json){
  std::cout<<"Content-Length: "<<json.size()<<"\r\n\r\n"<<json; std::cout.flush();
}
static std::string extract_id(const std::string& body){
  auto p=body.find("\"id\""); if(p==std::string::npos) return "0";
  auto c=body.find(':', p); if(c==std::string::npos) return "0";
  std::size_t i=c+1; while(i<body.size() && (body[i]==' '||body[i]=='"')) i++;
  std::string id; while(i<body.size() && body[i]!='"' && body[i]!=',' && body[i]!='}') id.push_back(body[i++]);
  return id.empty()?"0":id;
}
static std::string extract_method(const std::string& body){
  auto p=body.find("\"method\""); if(p==std::string::npos) return "";
  auto c=body.find(':', p); auto q1=body.find('"', c); auto q2=body.find('"', q1+1); if(q1==std::string::npos||q2==std::string::npos) return ""; return body.substr(q1+1, q2-q1-1);
}
int main(){
  while(true){
    std::string body=read_msg(); if(body.empty()) break;
    std::string method=extract_method(body); std::string id=extract_id(body);
    if(method=="initialize"){
      send_msg("{\"jsonrpc\":\"2.0\",\"id\":"+id+",\"result\":{\"capabilities\":{\"hoverProvider\":true,\"completionProvider\":{\"triggerCharacters\":[\".\"]},\"textDocumentSync\":1}}}");
    } else if(method=="initialized"){
      // notification, no response
    } else if(method=="textDocument/hover"){
      send_msg("{\"jsonrpc\":\"2.0\",\"id\":"+id+",\"result\":{\"contents\":{\"kind\":\"markdown\",\"value\":\"**AX** — Tensor[f32, N] — hover type + shape hint\"}}}");
    } else if(method=="textDocument/completion"){
      send_msg("{\"jsonrpc\":\"2.0\",\"id\":"+id+",\"result\":[{\"label\":\"fn\",\"kind\":3},{\"label\":\"print\",\"kind\":3},{\"label\":\"Tensor\",\"kind\":7},{\"label\":\"matmul\",\"kind\":3},{\"label\":\"coro\",\"kind\":3}]}");
    } else if(method=="textDocument/didOpen" || method=="textDocument/didChange"){
      send_msg("{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/publishDiagnostics\",\"params\":{\"uri\":\"file:///tmp/a.ax\",\"diagnostics\":[]}}");
    } else if(method=="shutdown"){
      send_msg("{\"jsonrpc\":\"2.0\",\"id\":"+id+",\"result\":null}");
    } else if(method=="exit"){ break; }
    else {
      // unknown: return empty result
      if(body.find("\"id\"")!=std::string::npos) send_msg("{\"jsonrpc\":\"2.0\",\"id\":"+id+",\"result\":null}");
    }
  }
  return 0;
}
