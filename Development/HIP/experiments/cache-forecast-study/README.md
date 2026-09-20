# CPU-only causal forecast screening

Run `python3 analyze.py CAPTURE_ROOT GAIN_F32 OUTPUT_DIR`. Requires NumPy and existing vit-residual capture-900-s0..s4 feature files. Loads true x/y but predictor updates only at forced actual frames0/4/8; future outputs are used only for evaluation. Measures relative ViT feature error on real375 tokens, not padded tokens, not RGB or GPU timing. See Development/results/cache-forecast-20260920/research.md for sources, interpretation and next tests. No game files or HIP modules are changed.
