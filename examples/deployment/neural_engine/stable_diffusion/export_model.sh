#!/bin/bash
# -*- coding: utf-8 -*-
#
# Copyright (c) 2023 Intel Corporation
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#   http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

for var in "$@"
  do
    case $var in
      --input_model=*)
          input_model=$(echo $var |cut -f2 -d=)
      ;;
      --precision=*)
          precision=$(echo $var |cut -f2 -d=)
      ;;
    esac
  done

# 1. text encoder
echo "[INFO] Start to export text encoder ir..."
python export_ir.py --onnx_model=${input_model}/text_encoder_${precision}/model.onnx --pattern_config=text_encoder_pattern.conf --output_path=./${precision}_ir/text_encoder/

# 2. unet
echo "[INFO] Start to export unet ir..."
python export_ir.py --onnx_model=${input_model}/unet_${precision}/model.onnx --pattern_config=unet_pattern.conf --output_path=./${precision}_ir/unet/

# 3. vae_decoder
echo "[INFO] start to export vae_decoder ir..."
python export_ir.py --onnx_model=${input_model}/vae_decoder_${precision}/model.onnx --pattern_config=vae_decoder_pattern.conf --output_path=./${precision}_ir/vae_decoder/
