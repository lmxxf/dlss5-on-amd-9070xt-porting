#!/usr/bin/env python3
"""Frame-to-frame input vs output difference from the DLSS5_FLICKER_DUMP files.

usage: flicker_stats.py <dir> <pid> <first_frame>
Files: flicker-<pid>-<k>-color.rgba16f (1920x1080 RGBA16F, this frame's network input),
       flicker-<pid>-<k>-history.f32   (1920x1080 float4, previous frame's network output),
       flicker-<pid>-<k>-motion.f32    (float4 per pixel, size gives the geometry).
For k -> k+1: input delta = color[k+1]-color[k]; output delta = history[k+2]-history[k+1]
(history[k+1] is the output for input k). Stats over all pixels and over near-static pixels
(|motion| < 0.5 px) so sub-pixel jitter amplification shows up separately from real motion.
"""
import sys, os, numpy as np
W, H = 1920, 1080

def load(path, dtype, comps):
    a = np.fromfile(path, dtype=dtype)
    if a.size % comps: raise SystemExit(f"{path}: size {a.size} not a multiple of {comps}")
    return a.reshape(-1, comps).astype(np.float32)

def stats(d):
    m = np.abs(d).max(axis=1)
    return dict(mean=float(m.mean()), p99=float(np.percentile(m, 99)), frac_1_255=float((m > 1/255).mean()), frac_4_255=float((m > 4/255).mean()))

def main():
    d, pid, first = sys.argv[1], sys.argv[2], int(sys.argv[3])
    frames = {}
    for k in range(first, first + 5):
        p = os.path.join(d, f"flicker-{pid}-{k}")
        if not os.path.exists(p + "-color.rgba16f"): continue
        color = load(p + "-color.rgba16f", np.float16, 4)[:, :3]
        hist = load(p + "-history.f32", np.float32, 4)[:, :3]
        motion = load(p + "-motion.f32", np.float32, 4)
        if color.shape[0] != W * H or hist.shape[0] != W * H: raise SystemExit("unexpected frame size")
        frames[k] = (color, hist, motion)
    keys = sorted(frames)
    print("frames:", keys)
    for k in keys:
        if k + 2 not in frames or k + 1 not in frames: continue
        c0, _, m0 = frames[k]; c1, h1, _ = frames[k + 1]; _, h2, _ = frames[k + 2]
        din = c1 - c0; dout = h2 - h1
        # motion buffer may be a different resolution; use it only when it matches
        static = None
        if m0.shape[0] == W * H:
            static = np.hypot(m0[:, 0], m0[:, 1]) < 0.5
        for name, mask in (("all", None), ("static", static)):
            if mask is None and name == "static": continue
            si = stats(din if mask is None else din[mask]); so = stats(dout if mask is None else dout[mask])
            cov = "" if mask is None else f" pixels={mask.mean():.2f}"
            print(f"{k}->{k+1} {name:6s}{cov}")
            print(f"   input  mean={si['mean']:.5f} p99={si['p99']:.4f} >1/255={si['frac_1_255']:.3f} >4/255={si['frac_4_255']:.3f}")
            print(f"   output mean={so['mean']:.5f} p99={so['p99']:.4f} >1/255={so['frac_1_255']:.3f} >4/255={so['frac_4_255']:.3f}  gain(mean)={so['mean']/max(si['mean'],1e-9):.2f}")
    # per-pixel amplification map summary on the middle pair
    if len(keys) >= 3:
        k = keys[0]; c0 = frames[k][0]; c1, h1 = frames[k+1][0], frames[k+1][1]; h2 = frames[k+2][1]
        i = np.abs(c1 - c0).max(axis=1); o = np.abs(h2 - h1).max(axis=1)
        quiet = i < 0.5/255
        print(f"pixels with input change <0.5/255: {quiet.mean():.3f}; among them output change >2/255: {(o[quiet] > 2/255).mean():.3f}, >8/255: {(o[quiet] > 8/255).mean():.3f}")

if __name__ == "__main__":
    main()
