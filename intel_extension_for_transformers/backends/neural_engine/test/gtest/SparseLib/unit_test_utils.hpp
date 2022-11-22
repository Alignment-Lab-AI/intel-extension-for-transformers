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

#ifndef ENGINE_TEST_GTEST_SPARSELIB_UNIT_TEST_UTILS_HPP_
#define ENGINE_TEST_GTEST_SPARSELIB_UNIT_TEST_UTILS_HPP_

#include <glog/logging.h>
#include <chrono>  // NOLINT
#include <stdexcept>
#include <cstring>
#include <iostream>
#include <set>
#include <vector>
#include <string>

#include "interface.hpp"

enum memo_mode { MALLOC, MEMSET };

using bfloat16_t = jd::bfloat16_t;

bfloat16_t fp32_2_bf16(float float_val) { return (*reinterpret_cast<unsigned int*>(&float_val)) >> 16; }

float bf16_2_fp32(bfloat16_t bf16_val) {
  unsigned int ans = bf16_val << 16;
  return *reinterpret_cast<float*>(&ans);
}

int uint8_2_int32(uint8_t a) {
  int ans = a;
  return ans;
}

float rand_float_postfix() { return rand() / static_cast<float>(RAND_MAX); }

void assign_val(void* ptr, jd::data_type dtype, float val, int idx) {
  switch (dtype) {
    case jd::data_type::fp32:
      *(reinterpret_cast<float*>(ptr) + idx) = val;
      break;
    case jd::data_type::bf16:
      *(reinterpret_cast<bfloat16_t*>(ptr) + idx) = fp32_2_bf16(val);
      break;
    case jd::data_type::u8:
      *(reinterpret_cast<uint8_t*>(ptr) + idx) = (uint8_t)val;
      break;
    case jd::data_type::s8:
      *(reinterpret_cast<int8_t*>(ptr) + idx) = (int8_t)val;
      break;
    default:
      std::runtime_error(std::string("assign_val:unsupport this dtype."));
  }
}

void* sparselib_ut_memo(void* ptr, int num, jd::data_type dtype, memo_mode mode) {
  int data_width = get_data_size(dtype);
  switch (mode) {
    case MALLOC:
      ptr = aligned_alloc(64, num * data_width); /* code */
      break;
    case MEMSET:
      std::memset(ptr, 0, num * data_width);
      break;
    default:
      break;
  }
  return ptr;
}

class n_thread_t {
 public:
  explicit n_thread_t(int nthr) : prev_nthr(omp_get_max_threads()) {
    if (nthr > 0 && nthr != prev_nthr) omp_set_num_threads(nthr);
  }
  ~n_thread_t() { omp_set_num_threads(prev_nthr); }

 private:
  int prev_nthr;
};

#endif  // ENGINE_TEST_GTEST_SPARSELIB_UNIT_TEST_UTILS_HPP_
