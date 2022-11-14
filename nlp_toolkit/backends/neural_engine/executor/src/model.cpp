//  Copyright (c) 2021 Intel Corporation
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

#include "model.hpp"

namespace executor {

Model::Model(const ModelConfig& conf, const string& weight_root) : weight_root_(weight_root) { Init(conf); }

Model::Model(const string& conf_file, const string& weight_root) : weight_root_(weight_root) {
  ModelConfig conf = ModelConfig(conf_file);
  CHECK_EQ(conf.CheckConfig(), true) << "model config not right....";
  Init(conf);
}

Model::~Model() {
  if (engine_profiling_) {
    LOG(INFO) << "Neural engine profiling ...";
    Profiling_ ProfilingWriter = Profiling_();
    ProfilingWriter.WriteProfiling(operators_, input_vecs_, output_vecs_);
  }
  if (MemoryAllocator::SharedEnv()) {
    RemoveSharedWeight(false);
  }
}

void Model::Init(const ModelConfig& conf) {
  // Clear the whole dnnl primitive cache map when init engine
  InnerProductPrimitiveFwdFactory::ClearFactory();
  MatMulPrimitiveFwdFactory::ClearFactory();
  ConvolutionPrimitiveFwdFactory::ClearFactory();
  InitSharedWeight();
  name_ = conf.name();
  MemoryAllocator::InitStrategy();
  // For each operator, set up its input and output
  auto op_configs = conf.operators();
  input_vecs_.resize(op_configs.size());
  output_vecs_.resize(op_configs.size());
  // Basically, build all the operators and set up their connections.
  for (int operator_id = 0; operator_id < op_configs.size(); ++operator_id) {
    auto op_conf = op_configs[operator_id];
    auto operator_name = op_conf->name();
    operators_.push_back(std::make_shared<Dispatcher>(*op_conf));
    operator_names_.push_back(operator_name);
    operator_name_index_[operator_name] = operator_id;
    // handle the input/output tensors to the model
    // we will have an input operator only have output data
    // in a graph the input tensors must come from output tensors
    // so we create output tensors and input tensors all take from output
    // tensors besides, we have two operators, one is Input and the other is
    // Output Input only have output tensors and Output only have input tensors
    // we treat weight tensors as output from Input operator and other
    // operators' input
    auto op_type = op_conf->type();

    int output_size = op_conf->output_tensor_size();
    for (int output_id = 0; output_id < output_size; ++output_id) {
      SetOutput(op_configs, operator_id, output_id, &tensor_name_index_);
    }
    int input_size = op_conf->input_tensor_size();
    for (int input_id = 0; input_id < input_size; ++input_id) {
      SetInput(op_configs, operator_id, input_id, &tensor_name_index_);
    }
  }
  // for debug tensor life
  for (size_t i = 0; i < tensors_.size(); ++i) {
    LOG(INFO) << "tensor name is " << tensors_[i]->name() << " tensor life is  " << tensors_[i]->life();
  }
  // prepare the operator like cache weight
  for (int i = 0; i < operators_.size(); ++i) {
    operators_[i]->Prepare(input_vecs_[i], output_vecs_[i]);
    // for profiling post op
    auto attrs = operators_[i]->operator_conf().attributes();
    if (attrs.find("append_op") != attrs.end()) {
      operators_[i]->set_post_op(attrs["append_op"]);
    }
    operators_[i]->set_attrs(attrs);
  }
  if (multi_stream_flag) {
    multi_stream_tasks_.clear();
    for (int i = 0; i < operators_.size(); ++i) {
      auto op_attr_map = operators_[i]->operator_conf().attributes();
      auto it = op_attr_map.find("multi_stream");
      if (it != op_attr_map.end()) {
        multi_stream_tasks_.insert({i, StringToNum<int64_t>(it->second)});
      }
    }
    auto max_tasks = std::max_element(multi_stream_tasks_.begin(), multi_stream_tasks_.end(),
        [] (const std::pair<int, int64_t>& a, const std::pair<int, int64_t>& b)
           ->bool{ return a.second < b.second; } );
    int tp_max_threads = max_tasks->second + (max_tasks->second & 1);
    int total_available_threads = omp_get_num_procs();
    tp_max_threads = tp_max_threads > total_available_threads ?
                                      total_available_threads : tp_max_threads;
    tp.begin(tp_max_threads);
    LOG(INFO) << "Thread pool is initialized with " << tp_max_threads << " threads. (" <<
                 "Total avaiable threads: " << total_available_threads << ")";
  }

  engine_profiling_ = (getenv("ENGINE_PROFILING") != NULL);  // profiling env
  is_dispatcher_tuning_ = (getenv("ENGINE_DISPATCHER_TUNING_ON") != NULL);
  char* env_root = getenv("ENGINE_DISPATCH_TABLE_FILE_ROOT");
  if (env_root == NULL && getenv("HOME") == NULL) {
    LOG(ERROR) << "Please export ENGINE_DISPATCH_TABLE_FILE_ROOT or HOME";
  }
  dispatch_table_file_root_ = env_root == NULL ? \
      string(getenv("HOME")) + "/.cache/neural_engine_workspace/engine_dispatch_table.txt" : \
      env_root;
  has_dispatch_table_file_ = (access(dispatch_table_file_root_.c_str(), F_OK) != -1);
  if (!has_dispatch_table_file_) LOG(INFO) << "Missing dispatch table file, " \
                                  "all operators will use their own default kernels." \
                                  "Recommend to turn on the tuning mode for better performance." \
                                  "Ignore above info if you are doing tuning...";
}

void Model::RemoveSharedWeight(bool is_begin, char* count_space_name, char* count_name, char* space_name) {
  LOG(INFO) << "Shared instance number: " << MemoryAllocator::InstNum();
  ipc::managed_shared_memory count_shm(ipc::open_or_create, count_space_name, 512);
  int* removed_count = count_shm.find_or_construct<int>(count_name)[sizeof(int)](0);
  rmutex_.lock();
  (*removed_count)++;
  if (is_begin) {  // In model init, remove shared space at the first thread
    if (*removed_count == 1) {
      ipc::shared_memory_object::remove(space_name);
    }
    if (*removed_count == MemoryAllocator::InstNum()) {
      ipc::shared_memory_object::remove(count_space_name);
    }
  } else {  // In model release, remove shared space at the last thread
    if (*removed_count == MemoryAllocator::InstNum()) {
      ipc::shared_memory_object::remove(space_name);
      ipc::shared_memory_object::remove(count_space_name);
    }
  }
  rmutex_.unlock();
}

void Model::InitSharedWeight(char* space_name) {
  if (MemoryAllocator::SharedEnv()) {
    RemoveSharedWeight(true);
    std::ifstream inFile(weight_root_, std::ios::in | std::ios::binary);
    size_t weight_size =
        inFile ? static_cast<size_t>(inFile.seekg(0, std::ios::end).tellg()) : static_cast<size_t>(weight_root_.size());
    // 2 * weight_size: an empirical value to check weight buffers could be allocated enough in shared memory
    static ipc::managed_shared_memory managed_shm(ipc::open_or_create, space_name, 2 * weight_size);
  }
}

ipc::managed_shared_memory::handle_t Model::LoadSharedWeight(const string& root, const string& type,
                                                             const vector<int64_t>& shape,
                                                             const vector<int64_t>& location) {
  int64_t size = Product(shape);
  int64_t bytes = size * type2bytes[type];
  string weight_name = std::to_string(location[0]) + std::to_string(location[1]);
  std::ifstream inFile(root, std::ios::in | std::ios::binary);
  void* shm_ptr = MemoryAllocator::ManagedShm().find_or_construct<char>(weight_name.c_str())[bytes](0);
  if (inFile) {
    inFile.seekg(location[0], std::ios::beg);
    inFile.read(reinterpret_cast<char*>(shm_ptr), location[1]);
    inFile.close();
  } else {
    std::memcpy(shm_ptr, &root[location[0]], location[1]);
  }
  const auto& handle = MemoryAllocator::ManagedShm().get_handle_from_address(shm_ptr);
  return handle;
}

void Model::SetInput(const vector<shared_ptr<OperatorConfig>>& conf, const int operator_id, const int tensor_id,
                     map<string, int>* tensor_name_index_) {
  // model input tensor not in output tensors
  auto op_conf = conf[operator_id];
  const string& tensor_name = op_conf->input_tensors(tensor_id)->name();
  if (!tensor_name_index_->count(tensor_name)) {
    LOG(FATAL) << "Unknown input tensor " << tensor_name << ", operator " << op_conf->name() << ", input index "
               << tensor_id;
  }
  const int id = (*tensor_name_index_)[tensor_name];
  // add tensor life count for memory handling
  tensors_[id]->add_tensor_life(1);
  input_vecs_[operator_id].push_back(tensors_[id]);
  // set model output tensors, it maybe a little strange as Output operator only
  // have input and the input is MODEL's output
  const string& op_type = op_conf->type();
  if (op_type == "Output") {
    model_output_tensors_.push_back(tensors_[id]);
    output_tensors_.push_back(Tensor(nullptr, tensors_[id]->shape(), tensors_[id]->dtype()));
  }
}

void Model::SetOutput(const vector<shared_ptr<OperatorConfig>>& conf, const int operator_id, const int tensor_id,
                      map<string, int>* tensor_name_index_) {
  auto op_conf = conf[operator_id];
  const string& tensor_name = op_conf->output_tensors(tensor_id)->name();
  if (tensor_name_index_->count(tensor_name)) {
    LOG(FATAL) << "duplicate output tensor name..." << tensor_name;
  }
  // start from output tensor
  auto tensor_config = op_conf->output_tensors(tensor_id);
  const int id = tensors_.size();
  Tensor* tensor_ptr(new Tensor(*tensor_config));
  tensors_.push_back(tensor_ptr);
  tensor_names_.push_back(tensor_name);
  output_vecs_[operator_id].push_back(tensor_ptr);
  (*tensor_name_index_)[tensor_name] = id;
  // set model input tensors, it maybe a little strange as Input operator only
  // have output and the output is MODEL's input
  const string& op_type = op_conf->type();
  if (op_type == "Input") {
    // parse weight here
    if (tensor_config->location().size() != 0) {
      if (MemoryAllocator::SharedEnv()) {
        auto handle =
            LoadSharedWeight(weight_root_, tensor_config->dtype(), tensor_config->shape(), tensor_config->location());
        tensor_ptr->set_shm_handle(handle);
      } else {
        void* weight_ptr =
            read_file_to_type(weight_root_, tensor_config->dtype(), tensor_config->shape(), tensor_config->location());
        tensor_ptr->set_data(weight_ptr);
      }
      return;
    }
    // set model input tensors
    model_input_tensors_.push_back(tensor_ptr);
    model_input_configs_.push_back(tensor_config);
  }
}

void Model::SetDispatchKernel(const bool& reshape_model) {
  if (is_dispatcher_tuning_) {
    for (int i = 0; i < operators_.size(); ++i) {
      operators_[i]->GetExecuteKernel(input_vecs_[i], output_vecs_[i], reshape_model,
                                      dispatch_table_file_root_, has_dispatch_table_file_);
    }
  } else {
    if (reshape_model) {
      for (int i = 0; i < operators_.size(); ++i) {
        operators_[i]->GetExecuteKernel(input_vecs_[i], output_vecs_[i], reshape_model,
                                        dispatch_table_file_root_, has_dispatch_table_file_);
      }
    }
  }

  // save dispatch table file after tuniung
  if (is_dispatcher_tuning_ && DispatchTable::Size() > 0) DispatchTable::Save(dispatch_table_file_root_);
}

vector<Tensor>& Model::Forward(vector<Tensor>& input_data) {
  CHECK_EQ(input_data.size(), model_input_tensors_.size())
      << "input data size not equal with model input tensor size....";
  // if we want use dynamic input data shape at run time, we should check the
  // input data shape and get the output shape, this should be necessary in each
  // Operator's Forward function
  bool reshape_model = false;
  for (int i = 0; i < input_data.size(); ++i) {
    vector<int64_t> data_shape = input_data[i].shape();
    // here we use model input configs to get the configured shape
    vector<int64_t> model_input_shape = model_input_configs_[i]->shape();
    vector<int64_t> origin_model_input = model_input_tensors_[i]->shape();
    LOG(INFO) << "data shape is " << data_shape[0] << " model config is " << model_input_shape[0] << " origin shape is "
              << origin_model_input[0];
    CHECK_EQ(data_shape.size(), model_input_shape.size()) << "input data should have same "
                                                          << "dimensions with configured model shape....";
    for (int axis = 0; axis < data_shape.size(); ++axis) {
      if (data_shape[axis] != origin_model_input[axis]) {
        // not equal case only happen when model input axis support dynamic in
        // config which axis value should be -1
        CHECK_EQ(model_input_shape[axis], -1) << "data shape mismatch " << data_shape[axis]
                                              << " while model input shape need " << model_input_shape[axis];
        reshape_model = true;
      }
    }
  }
  for (int i = 0; i < input_data.size(); ++i) {
  // model_input_tesnor_[i]->free_data();
    model_input_tensors_[i]->set_data(input_data[i].mutable_data());
    model_input_tensors_[i]->set_shape(input_data[i].shape());
  }

  SetDispatchKernel(reshape_model);

  if (!is_dispatcher_tuning_) {
    if (reshape_model&&engine_profiling_) {
        for (int i = 0; i < operators_.size(); ++i) {
        LOG(INFO) << "operator " << operators_[i]->name() << " gonna reshape with type " << operators_[i]->type();
        // get reshape time for profiling
        float start = Time("start");
        operators_[i]->Reshape(input_vecs_[i], output_vecs_[i]);
        float end = Time("end");
        operators_[i]->set_reshape_time(end - start);
      }
    } else if (!reshape_model&&engine_profiling_) {
        for (int i = 0; i < operators_.size(); ++i) {
        operators_[i]->set_reshape_time(0);
      }
    } else if (reshape_model) {
        for (int i = 0; i < operators_.size(); ++i) {
        LOG(INFO) << "operator " << operators_[i]->name() << " gonna reshape with type " << operators_[i]->type();
        operators_[i]->Reshape(input_vecs_[i], output_vecs_[i]);
      }
    }
    int thread_count = 1;
    if (engine_profiling_) {
      for (int i = 0; i < operators_.size(); ++i) {
        LOG(INFO) << "operator " << operators_[i]->name() << " gonna forward with type " << operators_[i]->type();
        if (multi_stream_flag && multi_stream_tasks_.find(i) != multi_stream_tasks_.end()) {
          float start = Time("start");
          tp.commitTask(std::bind(&executor::Dispatcher::Forward, operators_[i], input_vecs_[i], output_vecs_[i]));
          float end = Time("end");
          operators_[i]->set_latency(end - start);
          for (int j = 0; j < input_vecs_[i].size(); ++j) {
            operators_[i]->set_it_shape(input_vecs_[i][j]->shape());
          }
          if (i != operators_.size() - 1) {
            operators_[i]->set_ot_shape(output_vecs_[i][0]->shape());  // the last output is not exsit
          }
          LOG(INFO) << "operator: " << operators_[i]->name() << ", latency: " << end - start << " ms";
          if (thread_count >= multi_stream_tasks_[i]) {
            tp.waitAllTaskRunOver();
            thread_count = 0;
          }
          thread_count++;
        } else {
          float start = Time("start");
          operators_[i]->Forward(input_vecs_[i], output_vecs_[i]);
          float end = Time("end");
          // for profiling
          operators_[i]->set_latency(end - start);
          for (int j = 0; j < input_vecs_[i].size(); ++j) {
            operators_[i]->set_it_shape(input_vecs_[i][j]->shape());
          }
          if (i != operators_.size() - 1) {
            operators_[i]->set_ot_shape(output_vecs_[i][0]->shape());
          }
          LOG(INFO) << "operator: " << operators_[i]->name() << ", latency: " << end - start << " ms";
        }
      }
    } else {
      for (int i = 0; i < operators_.size(); ++i) {
        LOG(INFO) << "operator " << operators_[i]->name() << " gonna forward with type " << operators_[i]->type();
        if (multi_stream_flag && multi_stream_tasks_.find(i) != multi_stream_tasks_.end()) {
          tp.commitTask(std::bind(&executor::Dispatcher::Forward, operators_[i], input_vecs_[i], output_vecs_[i]));
          if (thread_count >= multi_stream_tasks_[i]) {
            tp.waitAllTaskRunOver();
            thread_count = 0;
          }
          thread_count++;
        } else {
          operators_[i]->Forward(input_vecs_[i], output_vecs_[i]);
        }
      }
    }
  }
  return this->output_tensors();
}

}  // namespace executor
