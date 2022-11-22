import os
import unittest
import neural_compressor.adaptor.pytorch as nc_torch
from distutils.version import LooseVersion
from intel_extension_for_transformers.optimization.pipeline import pipeline
from transformers import (
    AutoConfig,
    AutoTokenizer,
)

os.environ["WANDB_DISABLED"] = "true"
os.environ["GLOG_minloglevel"] = "2"
os.environ["DISABLE_MLFLOW_INTEGRATION"] = "true"

PT_VERSION = nc_torch.get_torch_version()
MODEL_NAME = "distilbert-base-uncased-finetuned-sst-2-english"

message = "The output scores should be close to 0.9999."


class TestPipeline(unittest.TestCase):
    @classmethod
    def tearDownClass(cls):
        from test_benchmark import TestBenchmark
        TestBenchmark.tearDownClass()

    def test_fp32_pt_model(self):
        text_classifier = pipeline(
            task="text-classification",
            model="distilbert-base-uncased-finetuned-sst-2-english",
            framework="pt",
        )
        outputs = text_classifier("This is great !")
        self.assertAlmostEqual(outputs[0]['score'], 0.9999, None, message, 0.0001)

    def test_int8_pt_model(self):
        text_classifier = pipeline(
            task="text-classification",
            model="Intel/distilbert-base-uncased-finetuned-sst-2-english-int8-static",
            framework="pt",
        )
        outputs = text_classifier("This is great !")
        self.assertAlmostEqual(outputs[0]['score'], 0.9999, None, message, 0.0001)


@unittest.skipIf(PT_VERSION >= LooseVersion("1.12.0"),
    "Please use PyTroch 1.11 or lower version for executor backend")
class TestExecutorPipeline(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        from test_benchmark import TestExecutorBenchmark
        TestExecutorBenchmark.setUpClass()
        cls.config = AutoConfig.from_pretrained(MODEL_NAME)
        cls.tokenizer = AutoTokenizer.from_pretrained(MODEL_NAME)

    @classmethod
    def tearDownClass(cls):
        from test_benchmark import TestExecutorBenchmark
        TestExecutorBenchmark.tearDownClass()

    def test_fp32_executor_model(self):
        text_classifier = pipeline(
            task="text-classification",
            config=self.config,
            tokenizer=self.tokenizer,
            model='tmp_trainer/fp32-model.onnx',
            model_kwargs={'backend': "executor"},
        )
        outputs = text_classifier(
            "But believe it or not , it 's one of the most "
            "beautiful , evocative works I 've seen ."
        )
        self.assertAlmostEqual(outputs[0]['score'], 0.9999, None, message, 0.0001)

    def test_int8_executor_model(self):
        text_classifier = pipeline(
            task="text-classification",
            config=self.config,
            tokenizer=self.tokenizer,
            model='tmp_trainer/int8-model.onnx',
            model_kwargs={'backend': "executor"},
        )
        outputs = text_classifier(
            "But believe it or not , it 's one of the most "
            "beautiful , evocative works I 've seen ."
        )
        self.assertAlmostEqual(outputs[0]['score'], 0.9999, None, message, 0.0001)


if __name__ == "__main__":
    unittest.main()
