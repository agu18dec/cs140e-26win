"""
Numeric gradient checker for TinyCNN backward pass.
Validates the backward math in Python before touching C.

Runs offline (no Pi needed).  Uses the Q10 weight binary if available,
otherwise uses small random weights.

Usage:
    python test_grads.py
    python test_grads.py model/asl_weights_q15.bin   # with real weights
"""

import sys
import struct
import math
import random

# ---------------------------------------------------------------------------
# Tiny numpy-free forward pass (matches asl-train.c exactly)
# ---------------------------------------------------------------------------

def relu(x):
    return [max(0.0, v) for v in x]

def conv2d(inp, H, W, IC, OC, w, b, pad=1, ksize=3):
    """HWC layout.  w shape: [OC, IC, ksize, ksize] flattened row-major."""
    out = [0.0] * (H * W * OC)
    for oc in range(OC):
        woc_off = oc * IC * ksize * ksize
        for oh in range(H):
            for ow in range(W):
                acc = b[oc]
                for ic in range(IC):
                    wic_off = woc_off + ic * ksize * ksize
                    for kh in range(ksize):
                        ih = oh + kh - pad
                        if ih < 0 or ih >= H: continue
                        for kw in range(ksize):
                            iw = ow + kw - pad
                            if iw < 0 or iw >= W: continue
                            acc += inp[ih*W*IC + iw*IC + ic] * w[wic_off + kh*ksize + kw]
                out[oh*W*OC + ow*OC + oc] = acc
    return out

def maxpool2(inp, H, W, C):
    """HWC 2×2 stride-2 max pool."""
    OH, OW = H // 2, W // 2
    out = [0.0] * (OH * OW * C)
    for oh in range(OH):
        for ow in range(OW):
            for c in range(C):
                mx = inp[(oh*2)*W*C + (ow*2)*C + c]
                for kh in range(2):
                    for kw in range(2):
                        v = inp[(oh*2+kh)*W*C + (ow*2+kw)*C + c]
                        if v > mx: mx = v
                out[oh*OW*C + ow*C + c] = mx
    return out

def fc(inp, in_n, out_n, w, b):
    """out[j] = b[j] + sum_i inp[i] * w[j*in_n+i]"""
    out = [0.0] * out_n
    for j in range(out_n):
        acc = b[j]
        for i in range(in_n):
            acc += inp[i] * w[j*in_n + i]
        out[j] = acc
    return out

def softmax_ce(logits, label):
    max_l = max(logits)
    probs = [math.exp(l - max_l) for l in logits]
    s = sum(probs)
    probs = [p / s for p in probs]
    return -math.log(probs[label] + 1e-30), probs

def hwc_to_chw(inp, H, W, C):
    """HWC [H,W,C] → CHW [C,H,W] flattened."""
    out = [0.0] * (C * H * W)
    for c in range(C):
        for h in range(H):
            for w in range(W):
                out[c*H*W + h*W + w] = inp[h*W*C + w*C + c]
    return out

# ---------------------------------------------------------------------------
# Full model
# ---------------------------------------------------------------------------

class Model:
    def __init__(self):
        # Weights — set externally
        self.conv1_w = None; self.conv1_b = None   # [8,1,3,3]=72, [8]
        self.conv2_w = None; self.conv2_b = None   # [16,8,3,3]=1152, [16]
        self.conv3_w = None; self.conv3_b = None   # [32,16,3,3]=4608, [32]
        self.fc1_w   = None; self.fc1_b   = None   # [64,288]=18432, [64]
        self.fc2_w   = None; self.fc2_b   = None   # [24,64]=1536, [24]
        # Activation caches
        self._img = None
        self._c1 = self._p1 = None
        self._c2 = self._p2 = None
        self._c3 = self._p3 = None
        self._chw = None
        self._f1 = None
        self._logits = None

    def forward(self, img_u8, label):
        """Cached forward pass.  Returns CE loss."""
        self._img = [x / 255.0 for x in img_u8]

        self._c1 = relu(conv2d(self._img, 28, 28, 1, 8,  self.conv1_w, self.conv1_b))
        self._p1 = maxpool2(self._c1, 28, 28, 8)
        self._c2 = relu(conv2d(self._p1, 14, 14, 8, 16, self.conv2_w, self.conv2_b))
        self._p2 = maxpool2(self._c2, 14, 14, 16)
        self._c3 = relu(conv2d(self._p2, 7,  7,  16, 32, self.conv3_w, self.conv3_b))
        self._p3 = maxpool2(self._c3, 7, 7, 32)
        self._chw = hwc_to_chw(self._p3, 3, 3, 32)
        self._f1  = relu(fc(self._chw, 288, 64, self.fc1_w, self.fc1_b))
        self._logits = fc(self._f1, 64, 24, self.fc2_w, self.fc2_b)

        loss, _ = softmax_ce(self._logits, label)
        return loss

    def backward(self, label):
        """Compute analytical gradients.  Returns dict name→list."""
        # Step 1: d_logits
        _, probs = softmax_ce(self._logits, label)
        d_logits = list(probs)
        d_logits[label] -= 1.0

        # Step 2: FC2 grads + d_f1
        gfc2_b = list(d_logits)
        gfc2_w = [0.0] * (24 * 64)
        d_f1 = [0.0] * 64
        for j in range(24):
            dl = d_logits[j]
            for i in range(64):
                gfc2_w[j*64+i] += dl * self._f1[i]
                d_f1[i] += dl * self.fc2_w[j*64+i]

        # Step 3: ReLU backward (FC1)
        d_f1 = [d_f1[i] if self._f1[i] > 0 else 0.0 for i in range(64)]

        # Step 4: FC1 grads + d_chw
        gfc1_b = list(d_f1)
        gfc1_w = [0.0] * (64 * 288)
        d_chw = [0.0] * 288
        for j in range(64):
            df = d_f1[j]
            for i in range(288):
                gfc1_w[j*288+i] += df * self._chw[i]
                d_chw[i] += df * self.fc1_w[j*288+i]

        # Step 5: Transpose backward (CHW → HWC)
        d_p3 = [0.0] * 288
        for c in range(32):
            for h in range(3):
                for w in range(3):
                    d_p3[h*3*32 + w*32 + c] = d_chw[c*9 + h*3 + w]

        # Step 6+7: MaxPool3 backward + ReLU3
        d_c3 = [0.0] * (7*7*32)
        for oh in range(3):
            for ow in range(3):
                for c in range(32):
                    bih, biw = oh*2, ow*2
                    mx = self._c3[(oh*2)*7*32 + (ow*2)*32 + c]
                    for kh in range(2):
                        for kw in range(2):
                            v = self._c3[(oh*2+kh)*7*32 + (ow*2+kw)*32 + c]
                            if v > mx: mx = v; bih = oh*2+kh; biw = ow*2+kw
                    d_c3[bih*7*32 + biw*32 + c] += d_p3[oh*3*32 + ow*32 + c]
        d_c3 = [d_c3[i] if self._c3[i] > 0 else 0.0 for i in range(7*7*32)]

        # Step 8: Conv3 grads + d_p2
        gconv3_b = [0.0] * 32
        gconv3_w = [0.0] * (32 * 16 * 9)
        d_p2 = [0.0] * (7*7*16)
        for oc in range(32):
            for oh in range(7):
                for ow in range(7):
                    dc = d_c3[oh*7*32 + ow*32 + oc]
                    gconv3_b[oc] += dc
                    for ic in range(16):
                        for kh in range(3):
                            ih = oh + kh - 1
                            if ih < 0 or ih >= 7: continue
                            for kw in range(3):
                                iw = ow + kw - 1
                                if iw < 0 or iw >= 7: continue
                                widx = oc*16*9 + ic*9 + kh*3 + kw
                                gconv3_w[widx] += dc * self._p2[ih*7*16 + iw*16 + ic]
                                d_p2[ih*7*16 + iw*16 + ic] += dc * self.conv3_w[widx]

        # Step 9+10: MaxPool2 backward + ReLU2
        d_c2 = [0.0] * (14*14*16)
        for oh in range(7):
            for ow in range(7):
                for c in range(16):
                    bih, biw = oh*2, ow*2
                    mx = self._c2[(oh*2)*14*16 + (ow*2)*16 + c]
                    for kh in range(2):
                        for kw in range(2):
                            v = self._c2[(oh*2+kh)*14*16 + (ow*2+kw)*16 + c]
                            if v > mx: mx = v; bih = oh*2+kh; biw = ow*2+kw
                    d_c2[bih*14*16 + biw*16 + c] += d_p2[oh*7*16 + ow*16 + c]
        d_c2 = [d_c2[i] if self._c2[i] > 0 else 0.0 for i in range(14*14*16)]

        # Step 11: Conv2 grads + d_p1
        gconv2_b = [0.0] * 16
        gconv2_w = [0.0] * (16 * 8 * 9)
        d_p1 = [0.0] * (14*14*8)
        for oc in range(16):
            for oh in range(14):
                for ow in range(14):
                    dc = d_c2[oh*14*16 + ow*16 + oc]
                    gconv2_b[oc] += dc
                    for ic in range(8):
                        for kh in range(3):
                            ih = oh + kh - 1
                            if ih < 0 or ih >= 14: continue
                            for kw in range(3):
                                iw = ow + kw - 1
                                if iw < 0 or iw >= 14: continue
                                widx = oc*8*9 + ic*9 + kh*3 + kw
                                gconv2_w[widx] += dc * self._p1[ih*14*8 + iw*8 + ic]
                                d_p1[ih*14*8 + iw*8 + ic] += dc * self.conv2_w[widx]

        # Step 12+13: MaxPool1 backward + ReLU1
        d_c1 = [0.0] * (28*28*8)
        for oh in range(14):
            for ow in range(14):
                for c in range(8):
                    bih, biw = oh*2, ow*2
                    mx = self._c1[(oh*2)*28*8 + (ow*2)*8 + c]
                    for kh in range(2):
                        for kw in range(2):
                            v = self._c1[(oh*2+kh)*28*8 + (ow*2+kw)*8 + c]
                            if v > mx: mx = v; bih = oh*2+kh; biw = ow*2+kw
                    d_c1[bih*28*8 + biw*8 + c] += d_p1[oh*14*8 + ow*8 + c]
        d_c1 = [d_c1[i] if self._c1[i] > 0 else 0.0 for i in range(28*28*8)]

        # Step 14: Conv1 grads (no input grad)
        gconv1_b = [0.0] * 8
        gconv1_w = [0.0] * (8 * 1 * 9)
        for oc in range(8):
            for oh in range(28):
                for ow in range(28):
                    dc = d_c1[oh*28*8 + ow*8 + oc]
                    gconv1_b[oc] += dc
                    for kh in range(3):
                        ih = oh + kh - 1
                        if ih < 0 or ih >= 28: continue
                        for kw in range(3):
                            iw = ow + kw - 1
                            if iw < 0 or iw >= 28: continue
                            widx = oc*9 + kh*3 + kw
                            gconv1_w[widx] += dc * self._img[ih*28 + iw]

        return {
            'conv1_w': gconv1_w, 'conv1_b': gconv1_b,
            'conv2_w': gconv2_w, 'conv2_b': gconv2_b,
            'conv3_w': gconv3_w, 'conv3_b': gconv3_b,
            'fc1_w':   gfc1_w,   'fc1_b':   gfc1_b,
            'fc2_w':   gfc2_w,   'fc2_b':   gfc2_b,
        }


# ---------------------------------------------------------------------------
# Weight loading from Q10 binary
# ---------------------------------------------------------------------------

def load_weights_q10(path):
    with open(path, 'rb') as f:
        data = f.read()
    num_params, frac_bits = struct.unpack_from('<II', data, 0)
    assert num_params == 10 and frac_bits == 10, f"Bad header: {num_params}, {frac_bits}"
    scale = 1.0 / (1 << frac_bits)
    keys = ['conv1_w','conv1_b','conv2_w','conv2_b','conv3_w','conv3_b',
            'fc1_w','fc1_b','fc2_w','fc2_b']
    off = 8
    weights = {}
    for k in keys:
        n = struct.unpack_from('<I', data, off)[0]; off += 4
        vals = struct.unpack_from(f'<{n}h', data, off); off += n * 2
        weights[k] = [v * scale for v in vals]
    return weights


def make_random_model(seed=42):
    """Small random weights to avoid dead ReLUs."""
    rng = random.Random(seed)
    sizes = {
        'conv1_w': 72,   'conv1_b': 8,
        'conv2_w': 1152, 'conv2_b': 16,
        'conv3_w': 4608, 'conv3_b': 32,
        'fc1_w': 18432,  'fc1_b': 64,
        'fc2_w': 1536,   'fc2_b': 24,
    }
    return {k: [rng.gauss(0, 0.05) for _ in range(n)] for k, n in sizes.items()}


# ---------------------------------------------------------------------------
# Numeric gradient checker
# ---------------------------------------------------------------------------

def check_grad(m, img, label, name, param_list, precomp_grads, eps=1e-3, n_check=8):
    """Numeric gradient check using precomputed analytic gradients.

    precomp_grads: dict returned by m.backward() at the ORIGINAL weights.
    Uses combined absolute+relative tolerance to handle ReLU kinks (dead
    neurons show kinks in finite differences but are analytically zero —
    this is correct, not a bug).
    """
    analytic_all = precomp_grads[name]
    n = len(param_list)
    step = max(1, n // n_check)
    indices = list(range(step//2, n, step))[:n_check]

    max_err = 0.0
    for idx in indices:
        orig = param_list[idx]

        param_list[idx] = orig + eps
        lp = m.forward(img, label)

        param_list[idx] = orig - eps
        lm = m.forward(img, label)

        param_list[idx] = orig  # restore

        numeric = (lp - lm) / (2 * eps)
        analytic = analytic_all[idx]
        # Combined absolute+relative error: handles near-zero (dead-neuron) cases.
        # atol=0.05 covers the kink artefact at exactly-zero activations.
        err = abs(numeric - analytic) / max(abs(analytic), abs(numeric), 0.05)
        if err > max_err: max_err = err

    return max_err


def norm(v):
    return math.sqrt(sum(x*x for x in v))


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main():
    # Load weights
    if len(sys.argv) > 1:
        wpath = sys.argv[1]
        print(f"Loading weights from {wpath}")
        weights = load_weights_q10(wpath)
    else:
        print("No weight file given — using random weights (seed=42)")
        weights = make_random_model()

    m = Model()
    for k, v in weights.items():
        setattr(m, k, v)

    # Synthetic image: gradient pattern
    img = bytes([i % 256 for i in range(784)])
    label = 3

    # Forward pass
    loss = m.forward(img, label)
    print(f"Initial loss: {loss:.4f}  (random baseline = {math.log(24):.4f})")

    # Backward pass
    grads = m.backward(label)

    # Print gradient norms
    print("\nGradient norms:")
    for k, g in grads.items():
        print(f"  {k:<12s}  ||g||={norm(g):.4e}")

    # Numeric gradient check
    # Compute analytic gradients ONCE at the original weights, before any perturbation.
    m.forward(img, label)
    grads = m.backward(label)

    eps = 1e-3
    print(f"\nNumeric gradient check (eps={eps:.0e}, 8 indices per tensor):")
    print("  (atol=0.05 to tolerate ReLU-kink artefacts at exactly-zero activations)")
    all_pass = True
    param_map = {
        'conv1_w': m.conv1_w, 'conv1_b': m.conv1_b,
        'conv2_w': m.conv2_w, 'conv2_b': m.conv2_b,
        'conv3_w': m.conv3_w, 'conv3_b': m.conv3_b,
        'fc1_w':   m.fc1_w,   'fc1_b':   m.fc1_b,
        'fc2_w':   m.fc2_w,   'fc2_b':   m.fc2_b,
    }
    for name, plist in param_map.items():
        err = check_grad(m, img, label, name, plist, grads, eps=eps)
        ok = err < 0.5   # generous: catches sign errors and O(1) mistakes
        all_pass = all_pass and ok
        print(f"  {name:<12s}  max_err={err:.4e}  {'PASS' if ok else 'FAIL'}")

    print()
    if all_pass:
        print("ALL PASS")
    else:
        print("SOME TESTS FAILED — fix backward before writing C")
        sys.exit(1)

    # Sanity: can we overfit 1 sample with SGD?
    print("\nOverfit check (200 SGD steps, lr=0.01):")
    init_loss = m.forward(img, label)
    lr = 0.01
    for _ in range(200):
        m.forward(img, label)
        gs = m.backward(label)
        for k, plist in param_map.items():
            g = gs[k]
            for i in range(len(plist)):
                plist[i] -= lr * g[i]
    final_loss = m.forward(img, label)
    print(f"  init_loss={init_loss:.4f}  final_loss={final_loss:.4f}")
    if final_loss < init_loss * 0.5:
        print("  Overfit check: PASS")
    else:
        # With random weights, 200 steps may not fully overfit.
        # Check at least a 20% reduction (real pretrained weights will do better).
        if final_loss < init_loss * 0.8:
            print("  Overfit check: PASS (partial — use pretrained weights for full overfit)")
        else:
            print("  Overfit check: FAIL (loss barely moved — gradient is wrong direction)")
            sys.exit(1)


if __name__ == '__main__':
    main()
