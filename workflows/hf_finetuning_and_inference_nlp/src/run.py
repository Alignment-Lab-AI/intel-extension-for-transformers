# Copyright (C) 2022 Intel Corporation
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing,
# software distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions
# and limitations under the License.
#


from transformers import TrainingArguments
from transformers import logging as hf_logging
import yaml, argparse


hf_logging.set_verbosity_info()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--config_file", type=str, required=True)
    args = parser.parse_args()

    with open(args.config_file, "r") as f:
        data = yaml.safe_load(f)

    training_args = TrainingArguments(output_dir="./output_dir")
    for item in data["training_args"]:
        setattr(training_args, item, data["training_args"][item])

    for item in data["args"]:
        setattr(args, item, data["args"][item])

    kwargs = {"args": args, "training_args": training_args}

    if args.pipeline == "finetune":
        if args.finetune_impl == "trainer":
            from finetune_trainer import FinetuneTrainer
            finetune = FinetuneTrainer(**kwargs)
        elif args.finetune_impl == "itrex":
            from finetune_itrex import FinetuneItrex

            finetune = FinetuneItrex(**kwargs)
        else:
            error_msg = (
                f"Now only support trainer and itrex implementations "
                f"for fine-tuning pipeline. "
                f"Your input is {args.finetune_impl}."
            )
            raise ValueError(error_msg)

        finetune.e2e_finetune()
    elif args.pipeline == "inference":
        if args.infer_impl == "trainer":
            from infer_trainer import TrainerInfer
            infer = TrainerInfer(**kwargs)
        elif args.infer_impl == "itrex":
            from infer_itrex import ItrexInfer
            infer = ItrexInfer(**kwargs)
        else:
            error_msg = f"Now only support trainer and itrex implementation for inference pipeline."
            raise ValueError(error_msg)

        infer.e2e_infer()
    else:
        error_msg = f"Now only support finetune and inference pipeline."
        raise ValueError(error_msg)

if __name__ == "__main__":
    main()
