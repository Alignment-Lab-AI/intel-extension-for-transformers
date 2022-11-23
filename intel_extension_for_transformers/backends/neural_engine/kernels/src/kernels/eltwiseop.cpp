//  Copyright (c) 2022 Intel Corporation
//
//  Licensed under the Apache License, Version 2.0 (the "License");
//  you may not use this file except in compliance with the License.
//  You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
//  Unless required by applicable law or agreed to in writing, software
//  distributed under the License is distributed on an "AS IS" BASIS,
//  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
//  See the License for the specific language governing permissions and
//  limitations under the License.

#include "kernels/eltwiseop.hpp"
#include "utils.hpp"
namespace jd {

bool eltwiseop_kd_t::init() {
  params_.postop_attrs = op_desc_.apply_postops_list();

  int nthr = op_desc_.impl_nthr();
  params_.element_num_each_th = params_.element_num / nthr;
  params_.remain_element = params_.element_num - (nthr - 1) * params_.element_num_each_th;

  return true;
}

bool eltwiseop_k_t::init() {
  jit_eltwiseop_t* ker = nullptr;
  auto status = eltwiseop_kernel_create(&ker, derived_kd()->params());

  int nthr = derived_kd()->operator_desc().impl_nthr();
  for (int i = 0; i < nthr; i++) td.push_back(new ssd::eltwiseop_data_t());

  if (!status) {
    return false;
  }
  jit_kers_ = ker;

  return true;
}

bool eltwiseop_k_t::eltwiseop_kernel_create(jit_eltwiseop_t** ker_pp, const ssd::eltwiseop_param_t& param) {
  *ker_pp = new jit_eltwiseop_t(param);
  if (*ker_pp == nullptr) {
    return false;
  }
  return (*ker_pp)->create_kernel();
}

bool eltwiseop_k_t::execute(const std::vector<const void*>& rt_data) const {
  int nthr = kd()->operator_desc().impl_nthr();
  auto eltwise_params = derived_kd()->params();
  const auto& jit_impl = jit_kers_;

  auto src_offset = [&] { return get_data_size(derived_kd()->params().in_dt); };
  auto dst_offset = [&] { return get_data_size(derived_kd()->params().out_dt); };

#pragma omp parallel for
  for (int idx = 0; idx < nthr; idx++) {
    auto data_param = td[idx];
    data_param->src = const_cast<void*>(rt_data[0]) + idx * src_offset() * eltwise_params.element_num_each_th;
    data_param->dst = const_cast<void*>(rt_data[1]) + idx * dst_offset() * eltwise_params.element_num_each_th;
    if (idx != nthr - 1) {
      data_param->element_num = eltwise_params.element_num_each_th;
    } else {
      data_param->element_num = eltwise_params.remain_element;
    }
    (*jit_impl)(td[idx]);
  }

  return true;
}

}  // namespace jd

