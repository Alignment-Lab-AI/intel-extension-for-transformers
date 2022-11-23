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

#include "sparse_matmul/sparse_matmul.hpp"
#include "sparse_matmul/spmm_vnni.hpp"
#include "sparse_matmul/spmm_amx_bf16_x16.hpp"
#include "sparse_matmul/spmm_avx512f.hpp"
namespace jd {

double sparse_matmul_bench::calc_flop() const {
  const auto& src0_desc = ts_descs[ssd::WEI];
  const auto& src1_desc = ts_descs[ssd::SRC];
  int oc = src0_desc.shape()[0];
  int ic = src0_desc.shape()[1];

  // Since avx512f kernel performs activation x weight, the shape of weight tensor is {ic, oc}
  if (src0_desc.dtype() == jd::data_type::fp32 && src1_desc.dtype() == jd::data_type::fp32) {
    std::swap(oc, ic);
  }

  if (std::find(src1_desc.shape().begin(), src1_desc.shape().end(), ic) == src1_desc.shape().end()) {
    LOG(WARNING) << "ic is not found in SRC shape!\n";
    return 0.0;
  }
  const int other_dim =
      std::accumulate(src1_desc.shape().begin(), src1_desc.shape().end(), 1, std::multiplies<size_t>()) / ic;
  return static_cast<double>(oc) * other_dim * ic * 2;
}

bench_res_t sparse_matmul_bench::set_config(int argc, char** argv) {
  bench_res_t res;

  if (!strcmp(argv[0], "vnni")) {
    smb = std::make_shared<spmm_vnni_bench>();
  } else if (!strcmp(argv[0], "amx_bf16_x16")) {
#ifdef SPARSE_LIB_USE_AMX
    smb = std::make_shared<spmm_amx_bf16_x16_bench>();
#else
    LOG(ERROR) << "SPARSE_LIB_USE_AMX is off";
    return {bench_status::unimplemented};
#endif  // SPARSE_LIB_USE_AMX
  } else if (!strcmp(argv[0], "avx512f")) {
    smb = std::make_shared<spmm_avx512f_bench>();
  } else {
    LOG(ERROR) << "unknown kernel specification";
    return {bench_status::wrong_input};
  }
  return smb->set_config(--argc, ++argv);
}

}  // namespace jd
