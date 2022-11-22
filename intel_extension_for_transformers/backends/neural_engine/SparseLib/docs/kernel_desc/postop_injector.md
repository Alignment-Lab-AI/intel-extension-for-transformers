<a name="VRz3S"></a>
# Introduction
op-fusion is a very widely used optimization approach in Deep-Learning.Consider we have two ops,Conv and Relu,in traditional way,we apply Conv op firstly,then store the value to the memory,after that we load the value and apply Relu.Obviously there have a useless load&store operations,we can fuse the Conv&Relu to remove the useless I/O,this is the key idea about op-fusion.<br />In SparseLib,we will provide a new class named injector for the op-fusion.In the perspective of the developers who want to apply the op-fusion optimization,they can make injector as a member of their jit_kernel class and initalize it in the kernel class's construct function,when they want to apply postop,just need to  call **injector->vector_compute** and tell injector what registers has been used by **injector->escape_regs**.Besides,upper level user also should call **injector->prepare_table** to prepare the LUT which postop need in the end of thier xbyak kernel.<br />injector supports 8 operators currently,there are exp,tanh,gelu,relu,linear,quantize(fp32->u8/s8),dequantize(u8/s8->fp32) and look-up result from LUT(as experimental API now).Injector also supports a postop-chain for apply multiple postops sequentially.
<a name="vY7m9"></a>
# Proposal
<a name="ml53U"></a>
## SparseLib developer's perspective
<a name="PuqaO"></a>
### Framework changes
<a name="Bu07F"></a>
#### param_types.hpp
Add some new fields.The most important field is `postop_attr`,which indicate the postop's attribute developer want to apply,include data_type(e.g. fp32/bf16),op_type(e.g. element wise/binary wise),algo_type(e.g. Gelu/Relu),aplha(zero points for quantization),beta,sacle for some operators such as linear&quantize.
```cpp
enum class postop_alg : uint8_t { exp, gelu, tanh, gelu, relu, quantize, dequantize, linear, int8_lut };

enum class postop_type : uint8_t { eltwise };

// postop attribute for op-fusion
class postop_attr {
 public:
  data_type dt;
  postop_type op_type;
  postop_alg op_alg;
  float alpha = 0;
  float beta = 0;
  float scale = 0;

  postop_attr(){};

  postop_attr(const data_type& dt, const postop_type& op_type, const postop_alg& op_alg, float alpha = 0.0,
              float beta = 0.0, float scale = 0.0)
      : dt(dt), op_type(op_type), op_alg(op_alg), alpha(alpha), beta(beta), scale(scale) {}
};
```

##### alpha,beta,scale meaning
these 3 params only used in quantize,dequantize,linear,relu.  
The quantize's mathematical definition is fp32=saturate(round(int8/scale+zero_point)) and the dequantize's mathematical definition is int8=(fp32-zero_point)*scale.In these two operators,alpha represent zero_point,scale represent scale and beta is useless.  
The mathematical definition of linear is y=αx+β.attr's alpha represent alpha,beta represent beta and scale is useless.  
The relu's mathematical definition is as follow,attr's alpha represent alpha,beta and scale are useless.  
![](../imgs/relu_formula.svg)

<a name="raAMd"></a>
#### operator_desc.hpp
Add a new member `apply_postops_list_`store the `postop_attr` which user want to apply.
```cpp
class operator_desc {
 public:
  operator_desc()
      : ker_kind_(jd::kernel_kind::undef),
        ker_prop_(jd::kernel_prop::undef),
        eng_kind_(jd::engine_kind::undef),
        impl_nthr_(0),
        ts_descs_({}),
        attrs_({}),
        apply_postops_list_({}) {}
  operator_desc(const jd::kernel_kind& ker_kind, const jd::kernel_prop& ker_prop, const jd::engine_kind& eng_kind,
                const std::vector<tensor_desc>& ts_descs, const std::unordered_map<std::string, std::string>& attrs,
                const std::vector<postop_attr>& apply_postops_list = {})
      : ker_kind_(ker_kind),
        ker_prop_(ker_prop),
        eng_kind_(eng_kind),
        impl_nthr_((omp_get_max_threads() == omp_get_num_procs()) ? 1 : omp_get_max_threads()),
        ts_descs_(ts_descs),
        attrs_(attrs),
        apply_postops_list_(apply_postops_list) {}
    
  private: 
   std::vector<postop_attr> apply_postops_list_;
}
```
<a name="hZaPk"></a>
#### jit_eltwise_injector.hpp
I design a element-wise injector named eltwise_injector which can apply eltwise-postops.I maybe combine this injector into a new injector named postop-injector in the future,but at present we needn't because we only have element-wise postop now.Overdesign is harmful.<br />Here are the APIs which injector expose to the developer:<br />`eltwise_injector_init` used for injector initialization.<br />`vector_compute` used for execute the postop calculate, user can indicate the eltwiseop's idx to select the op which user want to apply, if the idx list is empty, the injector will apply all ops in postop-chian.<br />`escape_regs` used for tell injector which registers have been used in upper level kernel.All dst zmm registers should be registered.<br />
`escape_erase` used for remove the specify type register ID from used_regs set,if reg_idx is not given,this function will erase all IDs by default.   
`prepare_table` used for insert the LUT which injected code need in the end of the upper level kernel.  
```cpp
class jit_eltwise_injector {
 public:
  explicit jit_eltwise_injector(){};
  virtual ~jit_eltwise_injector() {}

  void eltwise_injector_init(jit_generator* ptr, const std::vector<postop_attr>& postop_attrs);
  void vector_compute(const Xbyak::Zmm& zmm_src, const std::vector<postop_attr>& postop_attrs,std::vector<int> postop_idxs = {});
  void escape_regs(reg_type type, int reg_idx);
  void escape_erase(reg_type type,int reg_idx=-1);
  void prepare_table();
};
```
<a name="AHBMr"></a>
### How to use the injector
let take the kernel `eltwiseop` as example.
<a name="EibWR"></a>
#### step0.Add a postop_attrs vector member in your params for pass the postop_attrs to the jit_kernel
```cpp
struct eltwiseop_param_t {
  size_t element_num;
  data_type dt;
  std::vector<postop_attr> postop_attrs;
};
```
<a name="xqgcY"></a>
#### step1.Make injector as a member of your jit_class and init it in your construct&param_init function.
```cpp
class jit_eltwiseop_t : public jit_generator {
 public:
  explicit jit_eltwiseop_t(const ssd::eltwiseop_param_t& param) : jit_generator(), param_(param) {
    eltwise_injector.eltwise_injector_init(this, param_.postop_attrs);
    assign_regs();
  }

 private:
  ssd::eltwiseop_param_t param_;
  jit_eltwise_injector eltwise_injector;
};
```

```cpp
bool eltwiseop_kd_t::init() {
  auto op_attr = op_desc_.attrs();
  params_.postop_attrs = op_desc_.apply_postops_list();
  return true;
}
```

<a name="zcZPl"></a>
#### step2.Tell the injector which registers have been used before you apply postops.
```cpp
void jit_eltwiseop_t::assign_regs() {
  remain_task_mask = Xbyak::Opmask(6);
  scratch_ = Xbyak::Reg64(r10);
  reg_src = Zmm(6);
  addr_src = r15;
  addr_dst = r14;
  reg_param = rdi;
  remain_element_num = rsi; 
    
  eltwise_injector.escape_regs(reg_type::mask, remain_task_mask.getIdx());
  eltwise_injector.escape_regs(reg_type::reg64, scratch_.getIdx());
  eltwise_injector.escape_regs(reg_type::zmm, reg_src.getIdx());
  eltwise_injector.escape_regs(reg_type::zmm, addr_src.getIdx());
  eltwise_injector.escape_regs(reg_type::zmm, addr_dst.getIdx());
}
```
**NOTE:Injector will avoid allocate special usage registers such as **`RCX,RDX,RSI,RDI,RSP`**. upper level op dose not need to tell injector the usage information of these registers.**
<a name="zfFIG"></a>
#### step3.Apply the postops where you want and then prepare the LUT at the end of the kernel.
```cpp
void jit_eltwiseop_t::generate() {
  this->preamble();
  load_params();
    
  //load data.
  vmovups(reg_src, ptr[addr_src]);
  eltwise_injector.vector_compute(reg_src, param_.postop_attrs);
  //store data.
  vmovups(ptr[addr_dst], reg_src);

  this->postamble();

  eltwise_injector.prepare_table();
}
```
**NOTE:The postops will be apply **`in-place`** and storing work is upper op's task.**
<a name="rV6bL"></a>
## SparseLib user's perspective
This is the guide about how to set op-fusion in UT in user's perspective.
<a name="IqCA0"></a>
#### step0.Prepare the postop_attr
```cpp
postop_attr fp32_gelu_attr{data_type::fp32, postop_type::eltwise, postop_alg::gelu};
postop_attr bf16_gelu_attr{data_type::bf16, postop_type::eltwise, postop_alg::gelu}; 
postop_attr fp32_gelu_attr{data_type::fp32, postop_type::eltwise, postop_alg::gelu};
postop_attr bf16_gelu_attr{data_type::bf16, postop_type::eltwise, postop_alg::gelu};
```
<a name="kyHEm"></a>
#### step1.Gen_case.
```cpp
  cases.push_back(
      {gen_case(kernel_kind::eltwiseop, kernel_prop::forward_inference, engine_kind::cpu, {data0_desc, data0_desc},
                {{"postop_list", "fp32_gelu+fp32_exp"}, mask_mock1, reg64_mock1, zmm_mock1},
                {fp32_gelu_attr, fp32_exp_attr}),
       false});
  cases.push_back(
      {gen_case(kernel_kind::eltwiseop, kernel_prop::forward_inference, engine_kind::cpu, {data1_desc, data1_desc},
                {{"postop_list", "bf16_gelu+bf16_exp"}, mask_mock1, reg64_mock1, zmm_mock1},
                {bf16_gelu_attr, bf16_exp_attr}),
       false});
```
**NOTE:please add a pair<"postop_list",dt1op1+dt2op2+...> in **`op_attrs`** field for kernel hasing.**
#### step2.Check result
```cpp
void get_true_data(const operator_desc& op_desc, const std::vector<const void*>& rf_data) {
  float* src = (float*)rf_data[0];
  float* dst = (float*)rf_data[1];
  auto attr = op_desc.apply_postops_list();

  for (int i = 0; i < num; i++) {
    float tmp = your_kernel_logic(src[i]);
    apply_postop_list(num, attr, tmp);
    dst[i]=tmp;
  }
}
```
**NOTE: **`apply_postop_list`** is from head file **`unit_test_utils.hpp`****
