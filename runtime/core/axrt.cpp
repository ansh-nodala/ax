#include "runtime/core/axrt.hpp"
#include "runtime/core/ai_kernels.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <limits>
#include <vector>
#include <algorithm>
static AxVal* make_val(){ AxVal* v=(AxVal*)malloc(sizeof(AxVal)); v->kind=0; v->d=0; v->arr=nullptr; v->len=0; v->s=nullptr; return v; }
static AxVal* make_tensor(const double* arr, int n){ AxVal* v=make_val(); v->kind=1; v->len=n; v->arr=(double*)malloc(sizeof(double)*(n>0?n:1)); for(int i=0;i<n;i++) v->arr[i]=arr[i]; return v; }
static bool ax_size_value(AxVal* value, std::size_t& result){
  const double raw=ax_to_double(value);
  if(!std::isfinite(raw)||raw<0.0||raw>=static_cast<double>(std::numeric_limits<std::size_t>::max())||std::floor(raw)!=raw) return false;
  result=static_cast<std::size_t>(raw);
  return true;
}
extern "C" AxVal* ax_scalar(double d){ AxVal* v=make_val(); v->kind=0; v->d=d; return v; }
extern "C" AxVal* ax_string(const char* s){ AxVal* v=make_val(); v->kind=2; v->s=strdup(s?s:""); return v; }
extern "C" AxVal* ax_tensor_vals(double* arr, int n){ return make_tensor(arr, n); }
extern "C" AxVal* ax_vec4(AxVal* a, AxVal* b, AxVal* c, AxVal* d){ double arr[4]={ax_to_double(a),ax_to_double(b),ax_to_double(c),ax_to_double(d)}; return make_tensor(arr,4); }
extern "C" AxVal* ax_len(AxVal* v){ if(!v) return ax_scalar(0); if(v->kind==1) return ax_scalar((double)v->len); if(v->kind==2) return ax_scalar((double)strlen(v->s?v->s:"")); return ax_scalar(0); }
extern "C" double ax_to_double(AxVal* v){ if(!v) return 0; if(v->kind==1 && v->len>0) return v->arr[0]; if(v->kind==2) return atof(v->s?v->s:"0"); return v->d; }
extern "C" int ax_truthy(AxVal* v){ if(!v) return 0; if(v->kind==2) return v->s && v->s[0]; if(v->kind==1) return v->len>0; return v->d!=0.0; }
extern "C" void ax_print(AxVal* v){ if(!v){ printf("null\n"); return; } if(v->kind==2) printf("%s\n", v->s?v->s:""); else if(v->kind==1){ printf("["); for(int i=0;i<v->len;i++){ if(i) printf(", "); printf("%g", v->arr[i]); } printf("]\n"); } else printf("%g\n", v->d); }
static AxVal* binop(AxVal* a, AxVal* b, char op){
  if(!a||!b) return ax_scalar(0);
  if(a->kind==1||b->kind==1){ int n=a->kind==1?a->len:b->len; std::vector<double> out(n); for(int i=0;i<n;i++){ double av=a->kind==1?a->arr[i]:ax_to_double(a); double bv=b->kind==1?b->arr[i]:ax_to_double(b); double r=0; if(op=='+') r=av+bv; else if(op=='-') r=av-bv; else if(op=='*') r=av*bv; else if(op=='/') r=bv!=0?av/bv:0; out[i]=r; } return make_tensor(out.data(), n); }
  double av=ax_to_double(a), bv=ax_to_double(b); double r=0; if(op=='+') r=av+bv; else if(op=='-') r=av-bv; else if(op=='*') r=av*bv; else if(op=='/') r=bv!=0?av/bv:0; return ax_scalar(r);
}
extern "C" AxVal* ax_add(AxVal* a, AxVal* b){ return binop(a,b,'+'); }
extern "C" AxVal* ax_sub(AxVal* a, AxVal* b){ return binop(a,b,'-'); }
extern "C" AxVal* ax_mul(AxVal* a, AxVal* b){ return binop(a,b,'*'); }
extern "C" AxVal* ax_div(AxVal* a, AxVal* b){ return binop(a,b,'/'); }
extern "C" AxVal* ax_mod(AxVal* a, AxVal* b){ double bv=ax_to_double(b); return ax_scalar(bv!=0?fmod(ax_to_double(a),bv):0); }
extern "C" AxVal* ax_neg(AxVal* a){ if(a&&a->kind==1){ std::vector<double> out(a->len); for(int i=0;i<a->len;i++) out[i]=-a->arr[i]; return make_tensor(out.data(), a->len); } return ax_scalar(-ax_to_double(a)); }
static AxVal* cmpop(AxVal* a, AxVal* b, int op){ double av=ax_to_double(a), bv=ax_to_double(b); bool r=false; if(op==0) r=av==bv; else if(op==1) r=av!=bv; else if(op==2) r=av<bv; else if(op==3) r=av>bv; else if(op==4) r=av<=bv; else if(op==5) r=av>=bv; return ax_scalar(r?1.0:0.0); }
extern "C" AxVal* ax_eq(AxVal* a, AxVal* b){ return cmpop(a,b,0); }
extern "C" AxVal* ax_ne(AxVal* a, AxVal* b){ return cmpop(a,b,1); }
extern "C" AxVal* ax_lt(AxVal* a, AxVal* b){ return cmpop(a,b,2); }
extern "C" AxVal* ax_gt(AxVal* a, AxVal* b){ return cmpop(a,b,3); }
extern "C" AxVal* ax_le(AxVal* a, AxVal* b){ return cmpop(a,b,4); }
extern "C" AxVal* ax_ge(AxVal* a, AxVal* b){ return cmpop(a,b,5); }
// Tensor ops: a is flat row-major, b is flat. For matmul we treat a as [n] x [n] if square, else 1D fallback.
extern "C" AxVal* ax_matmul(AxVal* a, AxVal* b){
  if(!a||!b) return ax_scalar(0);
  if(a->kind!=1||b->kind!=1) return ax_mul(a,b);
  // If both are 1D of same len, dot product
  if(a->len==b->len){ double s=0; for(int i=0;i<a->len;i++) s+=a->arr[i]*b->arr[i]; return ax_scalar(s); }
  // Generic: elementwise mul as fallback for 1D broadcasting
  return ax_mul(a,b);
}
extern "C" AxVal* ax_gemm(AxVal* a, AxVal* b){ return ax_matmul(a,b); }
extern "C" AxVal* ax_softmax(AxVal* a){
  if(!a||a->kind!=1) return ax_scalar(1);
  double mx=a->arr[0]; for(int i=1;i<a->len;i++) mx=std::max(mx,a->arr[i]);
  std::vector<double> out(a->len); double sum=0; for(int i=0;i<a->len;i++){ out[i]=exp(a->arr[i]-mx); sum+=out[i]; } for(auto &x: out) x/=sum; return make_tensor(out.data(), a->len);
}
extern "C" AxVal* ax_relu(AxVal* a){
  if(!a) return ax_scalar(0);
  if(a->kind==1){
    std::vector<double> out(a->len);
    for(int i=0;i<a->len;i++) out[i]=std::max(0.0,a->arr[i]);
    return make_tensor(out.data(), a->len);
  }
  return ax_scalar(std::max(0.0, ax_to_double(a)));
}
extern "C" AxVal* ax_layernorm(AxVal* a){
  if(!a||a->kind!=1) return ax_scalar(0);
  double mean=0;
  for(int i=0;i<a->len;i++) mean+=a->arr[i];
  mean/=a->len;
  double var=0;
  for(int i=0;i<a->len;i++){
    double d=a->arr[i]-mean;
    var+=d*d;
  }
  var/=a->len;
  double stdv=sqrt(var+1e-5);
  std::vector<double> out(a->len);
  for(int i=0;i<a->len;i++) out[i]=(a->arr[i]-mean)/stdv;
  return make_tensor(out.data(), a->len);
}
extern "C" AxVal* ax_rmsnorm(AxVal* a){
  if(!a||a->kind!=1||a->len<=0) return ax_scalar(0);
  std::vector<float> input(static_cast<std::size_t>(a->len));
  for(int i=0;i<a->len;i++) input[static_cast<std::size_t>(i)]=static_cast<float>(a->arr[i]);
  const auto normalized=ax::ai::rms_norm(input,1,input.size());
  std::vector<double> output(normalized.begin(),normalized.end());
  return make_tensor(output.data(),a->len);
}
extern "C" AxVal* ax_sigmoid(AxVal* a){ if(a&&a->kind==1){ std::vector<double> out(a->len); for(int i=0;i<a->len;i++) out[i]=1.0/(1.0+exp(-a->arr[i])); return make_tensor(out.data(), a->len); } return ax_scalar(1.0/(1.0+exp(-ax_to_double(a)))); }
extern "C" AxVal* ax_tanh(AxVal* a){ if(a&&a->kind==1){ std::vector<double> out(a->len); for(int i=0;i<a->len;i++) out[i]=tanh(a->arr[i]); return make_tensor(out.data(), a->len); } return ax_scalar(tanh(ax_to_double(a))); }
extern "C" AxVal* ax_rope(AxVal* a){
  if(!a||a->kind!=1||a->len<=0) return ax_scalar(0);
  std::vector<float> input(static_cast<std::size_t>(a->len));
  for(int i=0;i<a->len;i++) input[static_cast<std::size_t>(i)]=static_cast<float>(a->arr[i]);
  if(input.size()%2!=0) return make_tensor(a->arr,a->len);
  const auto rotated=ax::ai::rope(input,1,input.size(),0);
  std::vector<double> output(rotated.begin(),rotated.end());
  return make_tensor(output.data(),a->len);
}
extern "C" AxVal* ax_rope_offset(AxVal* a, AxVal* position_offset){
  if(!a||a->kind!=1||a->len<=0||a->len%2!=0) return ax_scalar(0);
  const double raw_offset=ax_to_double(position_offset);
  if(raw_offset<0.0||raw_offset>=static_cast<double>(std::numeric_limits<std::size_t>::max())||std::floor(raw_offset)!=raw_offset) return ax_scalar(0);
  std::vector<float> input(static_cast<std::size_t>(a->len));
  for(int i=0;i<a->len;i++) input[static_cast<std::size_t>(i)]=static_cast<float>(a->arr[i]);
  const auto rotated=ax::ai::rope(input,1,input.size(),static_cast<std::size_t>(raw_offset));
  std::vector<double> output(rotated.begin(),rotated.end());
  return make_tensor(output.data(),a->len);
}
extern "C" AxVal* ax_rope_ex(AxVal* a, AxVal* sequence_length,
                              AxVal* head_dimension, AxVal* position_offset){
  if(!a||a->kind!=1||a->len<=0) return ax_scalar(0);
  std::size_t sequence=0, dimension=0, offset=0;
  if(!ax_size_value(sequence_length,sequence)||!ax_size_value(head_dimension,dimension)||
     !ax_size_value(position_offset,offset)||dimension==0||dimension%2!=0||
     sequence>std::numeric_limits<std::size_t>::max()/dimension||
     sequence*dimension!=static_cast<std::size_t>(a->len)) return ax_scalar(0);
  std::vector<float> input(static_cast<std::size_t>(a->len));
  for(int i=0;i<a->len;i++) input[static_cast<std::size_t>(i)]=static_cast<float>(a->arr[i]);
  const auto rotated=ax::ai::rope(input,sequence,dimension,offset);
  std::vector<double> output(rotated.begin(),rotated.end());
  return make_tensor(output.data(),a->len);
}
extern "C" AxVal* ax_swiglu(AxVal* a){
  if(!a||a->kind!=1) return ax_scalar(0);
  std::vector<float> input(static_cast<std::size_t>(a->len));
  for(int i=0;i<a->len;i++) input[static_cast<std::size_t>(i)]=static_cast<float>(a->arr[i]);
  const auto gated=ax::ai::swiglu(input,input);
  std::vector<double> output(gated.begin(),gated.end());
  return make_tensor(output.data(),a->len);
}
extern "C" AxVal* ax_swiglu2(AxVal* gate, AxVal* up){
  if(!gate||!up||gate->kind!=1||up->kind!=1||gate->len!=up->len) return ax_scalar(0);
  std::vector<float> gate_values(static_cast<std::size_t>(gate->len));
  std::vector<float> up_values(static_cast<std::size_t>(up->len));
  for(int i=0;i<gate->len;i++){
    gate_values[static_cast<std::size_t>(i)]=static_cast<float>(gate->arr[i]);
    up_values[static_cast<std::size_t>(i)]=static_cast<float>(up->arr[i]);
  }
  const auto gated=ax::ai::swiglu(gate_values,up_values);
  std::vector<double> output(gated.begin(),gated.end());
  return make_tensor(output.data(),gate->len);
}
extern "C" AxVal* ax_geglu_self(AxVal* a){ return ax_geglu(a,a); }
extern "C" AxVal* ax_geglu(AxVal* gate, AxVal* up){
  if(!gate||!up||gate->kind!=1||up->kind!=1||gate->len!=up->len) return ax_scalar(0);
  std::vector<float> gate_values(static_cast<std::size_t>(gate->len));
  std::vector<float> up_values(static_cast<std::size_t>(up->len));
  for(int i=0;i<gate->len;i++){
    gate_values[static_cast<std::size_t>(i)]=static_cast<float>(gate->arr[i]);
    up_values[static_cast<std::size_t>(i)]=static_cast<float>(up->arr[i]);
  }
  const auto gated=ax::ai::geglu(gate_values,up_values);
  std::vector<double> output(gated.begin(),gated.end());
  return make_tensor(output.data(),gate->len);
}
static double gelu_value(double x){ return 0.5*x*(1.0+tanh(0.7978845608028654*(x+0.044715*x*x*x))); }
extern "C" AxVal* ax_gelu(AxVal* a){
  if(a&&a->kind==1){ std::vector<double> out(a->len); for(int i=0;i<a->len;i++) out[i]=gelu_value(a->arr[i]); return make_tensor(out.data(),a->len); }
  return ax_scalar(gelu_value(ax_to_double(a)));
}
extern "C" AxVal* ax_silu(AxVal* a){
  if(a&&a->kind==1){ std::vector<double> out(a->len); for(int i=0;i<a->len;i++) out[i]=a->arr[i]/(1.0+exp(-a->arr[i])); return make_tensor(out.data(),a->len); }
  double x=ax_to_double(a); return ax_scalar(x/(1.0+exp(-x)));
}
extern "C" AxVal* ax_mse_loss(AxVal* prediction, AxVal* target){
  if(!prediction||!target) return ax_scalar(0);
  int n=prediction->kind==1?prediction->len:(target->kind==1?target->len:1);
  if(n<=0) return ax_scalar(0);
  double sum=0;
  for(int i=0;i<n;i++){ double p=prediction->kind==1?prediction->arr[i]:ax_to_double(prediction); double t=target->kind==1?target->arr[i]:ax_to_double(target); double d=p-t; sum+=d*d; }
  return ax_scalar(sum/n);
}
extern "C" AxVal* ax_cross_entropy(AxVal* logits, AxVal* labels){
  if(!logits||!labels||logits->kind!=1||labels->kind!=1||logits->len<=0) return ax_scalar(0);
  int n=std::min(logits->len,labels->len); double mx=logits->arr[0];
  for(int i=1;i<n;i++) mx=std::max(mx,logits->arr[i]);
  double sum=0; for(int i=0;i<n;i++) sum+=exp(logits->arr[i]-mx);
  if(sum<=0) return ax_scalar(0);
  double loss=0;
  for(int i=0;i<n;i++) if(labels->arr[i]!=0) loss-=labels->arr[i]*((logits->arr[i]-mx)-log(sum));
  return ax_scalar(loss);
}
extern "C" AxVal* ax_adamw(AxVal* p, AxVal* g, double lr){
  if(!p||!g) return ax_scalar(0);
  if(p->kind==1){
    std::vector<double> out(p->len);
    for(int i=0;i<p->len;i++){
      double gv=i<g->len?g->arr[i]:0;
      out[i]=p->arr[i]-lr*gv - lr*0.01*p->arr[i];
    }
    return make_tensor(out.data(), p->len);
  }
  return ax_scalar(ax_to_double(p)-lr*ax_to_double(g));
}
extern "C" double ax_math_sqrt(double x){ return sqrt(x); }
extern "C" double ax_math_exp(double x){ return exp(x); }
extern "C" double ax_math_log(double x){ return log(x); }
extern "C" double ax_math_sin(double x){ return sin(x); }
extern "C" double ax_math_cos(double x){ return cos(x); }
