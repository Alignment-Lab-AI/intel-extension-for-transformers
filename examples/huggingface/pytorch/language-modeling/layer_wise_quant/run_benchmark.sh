#!/bin/bash
set -x

python -u run_clm_no_trainer.py \
    --model /models/gpt-j-6B \
    --output_dir ./saved_results \
    --batch_size 16 \
    --approach weight_only \
    --tasks "lambada_openai" \
    --accuracy \
    --int8 