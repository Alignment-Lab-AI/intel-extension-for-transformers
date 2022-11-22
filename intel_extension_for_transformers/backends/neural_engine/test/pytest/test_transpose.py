#!/usr/bin/env python
# -*- coding: utf-8 -*-
#
# Copyright (c) 2022 Intel Corporation
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

import unittest
from intel_extension_for_transformers.backends.neural_engine.compile.graph import Graph
from intel_extension_for_transformers.backends.neural_engine.compile import compile
import numpy as np
import os


class TestTranspose(unittest.TestCase):
    @classmethod
    def setUpClass(self):
        pass

    @classmethod
    def tearDownClass(self):
        pass

    def test_transpose(self):
        model_dir = '/home/tensorflow/localfile/nlptoolkit_ut_model/bert_mini_int8_original_IR'
        self.assertTrue(os.path.exists(model_dir),
            'INT8 IR model is not found, please set your own model path!')
        model = compile(model_dir)
        model.transpose_mode_int8()

if __name__ == "__main__":
    unittest.main()
