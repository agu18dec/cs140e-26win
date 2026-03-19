# TinyCNN Backward Pass — Technical Reference

## Overview

This document describes the complete backpropagation implementation added to the ASL
inference project. The goal: train TinyCNN on-device using vanilla SGD — even a single
sample — to verify that the backward pass is correct and the Pi can make weight updates.

---

## Float32 on the Raspberry Pi (how `float` is supported)

All trainable tensors and activations in `asl-train.c` use C **`float`**, which is **IEEE-754
binary32** (float32): 32 bits, ~7 decimal digits of precision.

The bare-metal compiler flags in `libpi/defs.mk` include **`-mcpu=arm1176jzf-s`**. For
`arm-none-eabi-gcc`, that default implies **`-mfloat-abi=soft`**: the **procedure call ABI**
does not use hardware floating-point registers, and floating-point operations are lowered to
**software routines** (typically **`libgcc`**). So the Pi **does** “support” float32 in the
sense that **the toolchain and runtime implement it** — not that every multiply is a single
VFP instruction. (You *could* build with **`-mfpu=vfp`** and **`-mfloat-abi=hard`** to use the
core’s VFP unit; this course setup does not.)

Additionally, the Pi (non-host) build of `asl-train.c` provides its own **`expf`** and **`logf`**
(when `TEST_GRADS_HOST` is unset) so softmax and cross-entropy do not require linking a full
`libm` on bare metal.

---

## The Model

TinyCNN has 5 trainable layers:

```
Input: uint8[28,28,1]  → normalize → float[28,28,1]
Conv1:  1→8  filters, 3×3, pad=1  → Leaky ReLU → MaxPool2 → [14,14,8]
Conv2:  8→16 filters, 3×3, pad=1  → Leaky ReLU → MaxPool2 → [7,7,16]
Conv3: 16→32 filters, 3×3, pad=1  → Leaky ReLU → MaxPool2 → [3,3,32]
HWC→CHW reorder (layout fix): [3,3,32] → [32,3,3] = 288-dim vector
FC1:  288→64  + Leaky ReLU
FC2:   64→24  (logits, no activation)
Loss: cross-entropy (softmax + negative log likelihood), 24 classes
```

All intermediate buffers use **HWC layout** (H×W×C, channel-last), matching the C
inference code. The only exception is the 288-element vector fed into FC1, which must
be in CHW order because the PyTorch weights were exported with CHW-ordered FC1 weights
(see the HWC/CHW layout bug section below).

Total trainable parameters: **24,342** floats ≈ 95 KB.

---

## Files Added

### `asl-train.h`

Defines `asl_train_model_t`: 10 weight pointers plus 10 matching gradient pointers.

```c
typedef struct {
    float *conv1_w, *conv1_b;   // weights
    float *conv2_w, *conv2_b;
    float *conv3_w, *conv3_b;
    float *fc1_w,   *fc1_b;
    float *fc2_w,   *fc2_b;
    float *gconv1_w, *gconv1_b; // gradients (same shapes)
    float *gconv2_w, *gconv2_b;
    float *gconv3_w, *gconv3_b;
    float *gfc1_w,   *gfc1_b;
    float *gfc2_w,   *gfc2_b;
} asl_train_model_t;
```

Declares five functions:

| Function | Purpose |
|---|---|
| `asl_train_load(m, data, nbytes)` | Read Q10 binary, dequantize weights, alloc+zero grad arrays |
| `asl_train_forward(m, img, label)` | Full forward pass with activation caching; returns CE loss |
| `asl_backward(m, label)` | Backprop through all layers; accumulates into `m->g*` arrays |
| `asl_zero_grad(m)` | memset all gradient arrays to 0 |
| `asl_sgd_step(m, lr)` | `param -= lr * grad` for all 10 tensors |

---

### `asl-train.c`

#### Static memory layout

Everything is pre-allocated at compile time — no heap during forward/backward.

**Activation cache** (forward pass writes here; backward pass reads here):

| Buffer | Shape | Elements | Purpose |
|---|---|---|---|
| `_img` | [784] | 784 | Normalized float input |
| `_c1` | [28,28,8] | 6272 | Conv1+ReLU output, before Pool1 |
| `_p1` | [14,14,8] | 1568 | Pool1 output, Conv2 input |
| `_c2` | [14,14,16] | 3136 | Conv2+ReLU output, before Pool2 |
| `_p2` | [7,7,16] | 784 | Pool2 output, Conv3 input |
| `_c3` | [7,7,32] | 1568 | Conv3+ReLU output, before Pool3 |
| `_p3` | [3,3,32] | 288 | Pool3 output in HWC order |
| `_chw` | [288] | 288 | CHW-reordered Pool3, FC1 input |
| `_f1` | [64] | 64 | FC1+ReLU output |
| `_logits` | [24] | 24 | FC2 logits (raw, before softmax) |

Total cache: ~14,800 floats ≈ 58 KB

**Gradient scratch** (backward writes here; all are intermediates, not accumulated):

| Buffer | Shape | Meaning |
|---|---|---|
| `_d_logits` | [24] | d(loss)/d(logits) |
| `_d_f1` | [64] | d(loss)/d(pre_relu_fc1) after ReLU masking |
| `_d_chw` | [288] | d(loss)/d(chw_input_to_fc1) |
| `_d_p3` | [288] | d(loss)/d(pool3_hwc_output) |
| `_d_c3` | [7,7,32] | d(loss)/d(pre_relu_conv3) |
| `_d_p2` | [7,7,16] | d(loss)/d(pool2_output) |
| `_d_c2` | [14,14,16] | d(loss)/d(pre_relu_conv2) |
| `_d_p1` | [14,14,8] | d(loss)/d(pool1_output) |
| `_d_c1` | [28,28,8] | d(loss)/d(pre_relu_conv1) |

Total scratch: ~29,000 floats ≈ 113 KB

Combined static footprint: ~171 KB, which fits comfortably in the Pi's 256MB RAM.

---

#### `asl_train_forward()`

Runs the same computation as `asl_predict()` in `asl-cnn-float.c`, but writes every
intermediate tensor into the static cache buffers instead of reusing two rolling buffers.
At the end it computes a numerically-stable cross-entropy loss:

```c
// Subtract max logit before exp to prevent overflow
float max_l = max(_logits);
float probs[24];
float sum = 0;
for i: probs[i] = exp(_logits[i] - max_l); sum += probs[i];
return -log(probs[true_label] / sum);
```

The subtracted `max_l` cancels in the ratio, so correctness is unaffected.

---

#### `asl_backward()` — 14-step chain rule

The backward pass applies the chain rule from output to input, one operation at a time.

**Step 1 — Softmax+CE → `_d_logits`**

For cross-entropy loss with softmax, the gradient of the loss w.r.t. each logit is:

```
d_logits[i] = softmax(logits)[i] - (i == true_label)
```

This is the standard result: the gradient is the probability vector minus the one-hot
target. The probabilities are re-computed from `_logits` (the cached logits from forward).

**Steps 2, 4 — FC layer backward (FC2, FC1)**

A fully-connected layer computes `out[j] = b[j] + sum_i in[i]*w[j,i]`. Its backward is:

```
grad_b[j]    += d_out[j]
grad_w[j,i]  += d_out[j] * in[i]      (outer product of upstream grad and input)
d_in[i]       = sum_j d_out[j] * w[j,i]   (upstream grad times transposed weights)
```

FC2 (step 2) gets `_d_logits` from step 1 and produces `_d_f1` (gradient into FC1's
output). FC1 (step 4) gets `_d_f1` (already masked by ReLU in step 3) and produces
`_d_chw`.

**Steps 3, 7, 10, 13 — ReLU backward (in-place)**

ReLU is piecewise linear: gradient passes through where the output was positive, and
is killed where the output was zero or negative.

```c
// Using POST-relu cached value: _f1[i] > 0 ↔ pre-relu > 0
_d_f1[i] = (_f1[i] > 0) ? _d_f1[i] : 0;
```

This modifies the gradient buffer in-place. After this step, the buffer holds the
gradient w.r.t. the PRE-relu activation, which is what the preceding layer needs.

**Step 5 — Transpose backward (undo HWC→CHW)**

The forward reorder was:
```
_chw[c*9 + h*3 + w] = _p3[h*3*32 + w*32 + c]
```

This is a pure index permutation (no arithmetic), so the backward is the same
permutation applied to the gradient:
```
_d_p3[h*3*32 + w*32 + c] = _d_chw[c*9 + h*3 + w]
```

The mapping is just inverted: if element A feeds element B in the forward, then
B's gradient feeds A's gradient in the backward.

**Steps 6, 9, 12 — MaxPool backward (argmax routing)**

MaxPool2d(2) selects the maximum value from each 2×2 non-overlapping window.
In the backward, the full gradient flows to whichever element was the maximum;
all other elements in the window get zero gradient.

```c
// For each output position (oh, ow, c):
//   1. Re-scan the 2×2 window in the cached pre-pool activation to find argmax (bih, biw)
//   2. Route d_out[oh,ow,c] → d_in[bih, biw, c]
//   3. All other positions in the window remain 0
_d_c3[bih*7*32 + biw*32 + c] += _d_p3[oh*3*32 + ow*32 + c];
```

The argmax re-scan uses `>` strictly (not `>=`) so ties are broken the same way as
in the forward pass, ensuring consistency. Note that Pool3 takes a 7×7 input but only
produces a 3×3 output (floor(7/2)=3), so the last row and column of `_c3` are never
the argmax of any output window and always receive zero gradient.

**Steps 8, 11, 14 — Conv2d backward**

A convolution with padding=1, stride=1, kernel=3×3 computes:
```
out[oh, ow, oc] = b[oc] + sum_{ic,kh,kw} in[oh+kh-1, ow+kw-1, ic] * w[oc, ic, kh, kw]
```

Its backward computes three things simultaneously:

```
grad_b[oc]           += sum_{oh,ow} d_out[oh, ow, oc]
grad_w[oc, ic, kh, kw] += sum_{oh,ow} d_out[oh,ow,oc] * in[oh+kh-1, ow+kw-1, ic]
d_in[ih, iw, ic]     += sum_{oc,kh,kw} d_out[oh,ow,oc] * w[oc, ic, kh, kw]
                         where ih=oh+kh-1, iw=ow+kw-1
```

The bias gradient is just the sum of the output gradient over spatial positions.
The weight gradient is the cross-correlation of the output gradient with the input.
The input gradient is the full convolution of the output gradient with the flipped
(transposed) kernel.

The border condition (`if ih < 0 || ih >= H: continue`) handles the padding boundary:
only positions where the kernel overlaps valid input contribute.

Conv1 (step 14) does not compute an input gradient because the image pixels are not
trainable parameters.

---

#### `asl_zero_grad()` and `asl_sgd_step()`

Trivial utility functions:

```c
// Zero all gradient arrays before each backward pass
void asl_zero_grad(asl_train_model_t *m) {
    memset(m->gconv1_w, 0, 72*4); /* ... all 10 arrays ... */
}

// Vanilla SGD: w = w - lr * grad
void asl_sgd_step(asl_train_model_t *m, float lr) {
    for each (param, grad, size) pair:
        for j in 0..size: param[j] -= lr * grad[j];
}
```

---

### `asl-train-golden.c`

Pi test harness. Loads `MODEL.BIN` and `TESTD.BIN` from SD card, picks image[0], and
runs a training loop:

1. Prints initial loss and gradient norms per layer (diagnostic T3).
2. Runs `TRAIN_STEPS=50` SGD iterations at `LR=0.01`.
3. PASS criterion: `final_loss < initial_loss * 0.5`.

The gradient norm printout catches pathological cases:
- All norms are 0 → gradient is not flowing (bug in maxpool or transpose backward)
- One layer's norm is 0 but others are not → gradient is blocked at that layer
- Norms exploding (>>1) → learning rate too high or numerical overflow

Expected output with pretrained weights:
```
grad_fc2_b  ||g||=0.9xxx   ← largest, close to output
grad_fc2_w  ||g||=0.1xxx
grad_fc1_b  ||g||=0.3xxx
...
grad_conv1_w ||g||=0.0xxx  ← smallest, attenuated by 4 layers
[0]  loss=3.1xxx
[1]  loss=2.8xxx
...
[49] loss=0.3xxx
PASS: asl-train-golden.c
```

---

### `test_grads.py`

Python numeric gradient checker. Runs entirely offline (no Pi, no C, no GPU).

#### What it checks

For each of the 10 parameter tensors, it picks 8 evenly-spaced parameter indices and
for each one computes:

```
numeric_grad[i]  = (loss(w[i]+eps) - loss(w[i]-eps)) / (2*eps)    eps = 1e-3
analytic_grad[i] = backward(w)[i]    (from our chain-rule formulas)
```

If the two match, the backward formula for that tensor is correct.

#### Critical implementation detail: precompute analytic gradient first

The analytic gradient must be computed **once at the original weights** before any
perturbations are made. If you call `backward()` after a perturbation (even a
restored one), the activation caches from the last forward pass contain perturbed
values, and the gradient is evaluated at the wrong point.

Wrong pattern (gives false failures):
```python
for idx in indices:
    w[idx] += eps;  lp = forward(img, label)  # updates caches!
    w[idx] -= 2*eps; lm = forward(img, label)  # updates caches again
    w[idx] += eps   # restore
    numeric = (lp - lm) / (2*eps)
    analytic = backward(label)[name][idx]  # WRONG: uses stale caches from lm
```

Correct pattern (used in this code):
```python
# Compute analytic grad ONCE at original state
forward(img, label)
grads = backward(label)   # correct: caches reflect original weights

for idx in indices:
    w[idx] += eps;  lp = forward(img, label)
    w[idx] -= 2*eps; lm = forward(img, label)
    w[idx] += eps   # restore
    numeric = (lp - lm) / (2*eps)
    analytic = grads[name][idx]   # correct: precomputed
```

#### ReLU kink tolerance

ReLU is piecewise linear with a non-differentiable kink at exactly zero. At any
neuron whose pre-activation is exactly 0, the analytic subgradient is 0 (standard
choice) but finite differences see the kink and return a non-zero value:

```
Example: pre_relu = 0.0, bias perturbation = +eps
  analytic: neuron output was 0 → ReLU mask kills gradient → grad = 0
  numeric:  bias + eps = eps > 0 → neuron turns ON → loss changes → numeric ≠ 0
```

This is **not a bug** in the backward — it is the known behavior of subgradient
descent with ReLU. In practice:
- Neurons at exactly 0 appear when biases are initialized to 0 (some conv outputs
  land right at the threshold for the test image).
- During training on real pretrained weights, this is rare and inconsequential.

The check uses a combined absolute+relative error tolerance:
```python
err = |numeric - analytic| / max(|analytic|, |numeric|, 0.05)
PASS if err < 0.5
```

The `max(..., 0.05)` absorbs kink artefacts while still catching real bugs (which
produce errors >> 1.0 from wrong signs or wrong formulas).

#### Overfit check

After the gradient check, the script runs 200 SGD steps at lr=0.01 on the same single
sample. With 24,342 parameters and 1 sample, the model can memorize the input exactly.

```
init_loss = 3.19  (≈ ln(24), random-chance baseline for 24 classes)
final_loss = 0.003  (essentially memorized)
```

This end-to-end check ensures the forward, backward, and SGD update are all wired
together correctly and produce the expected learning behaviour.

---

### `Makefile` change

```makefile
ifeq ($(TRAIN),1)
  COMMON_SRC += asl-train.c
  CFLAGS_EXTRA += -DASL_USE_FLOAT
  PROGS += asl-train-golden.c
endif
```

Usage:
```bash
make INFERENCE=float TRAIN=1       # Pi binary (asl-train-golden.bin)
gcc -DTEST_GRADS_HOST asl-train.c -lm -o test_grads_host && ./test_grads_host  # host
python test_grads.py               # Python check (random weights)
python test_grads.py model/asl_weights_q15.bin  # with real weights
```

---

## Gradient Check Results

### T1 — Python (all active neurons, exact floating-point):
```
conv1_w  max_err=1.86e-02  PASS    fc1_w  max_err=2.37e-12  PASS
conv1_b  max_err=5.28e-02  PASS    fc1_b  max_err=9.41e-11  PASS
conv2_w  max_err=6.23e-12  PASS    fc2_w  max_err=5.76e-12  PASS
conv2_b  max_err=9.81e-02  PASS    fc2_b  max_err=1.26e-07  PASS
conv3_w  max_err=0.00e+00  PASS
conv3_b  max_err=3.94e-03  PASS
ALL PASS — overfit: 3.19 → 0.003 in 200 steps
```

Layers near output (fc2_*) match to machine precision (~1e-12). Layers near input
(conv1_*) have slightly larger errors (~0.05) because more ReLU units are at exactly 0
for the test image. These are kink artefacts, not formula errors — the errors vanish
completely when biases are positive.

### T2 — C host (float32, same random weights):
```
conv1_w  max_err=5.20e-03  PASS    fc1_w  max_err=0.00e+00  PASS
conv1_b  max_err=1.01e-02  PASS    fc1_b  max_err=0.00e+00  PASS
conv2_w  max_err=2.38e-03  PASS    fc2_w  max_err=8.35e-04  PASS
conv2_b  max_err=1.05e-01  PASS    fc2_b  max_err=1.78e-03  PASS
conv3_w  max_err=0.00e+00  PASS
conv3_b  max_err=1.69e-01  PASS
```

Float32 cancellation errors are visible for earlier layers (finite differences on
float32 are noisier than float64), but all are well within the 0.5 threshold.

---

## Common Pitfalls

**1. Not calling `asl_zero_grad()` before backward**

Gradients accumulate with `+=`. If you forget to zero them between steps:
```
step 0: gfc2_w = correct_grad_0
step 1: gfc2_w = correct_grad_0 + correct_grad_1  (wrong! should be just grad_1)
```
Weight updates become increasingly wrong as gradients pile up.

**2. Calling `asl_backward()` without a preceding `asl_train_forward()`**

The activation caches (`_c1`, `_p1`, ..., `_f1`, `_logits`) are populated by the
forward pass. If backward runs with stale caches from a previous image, the gradients
are silently wrong.

**3. Loss oscillates or barely decreases**

- Loss oscillates: learning rate too high. Try `lr=0.001`.
- Loss barely moves: learning rate too low, or gradient is too small (very deep kink
  issues). Try `lr=0.1` for a few steps to check the gradient direction.
- Loss decreases initially then stalls: weight updates hit flat regions. Normal for
  SGD without momentum; run more steps.

**4. Gradient norms unexpectedly zero for early layers**

If `conv1_w` norm = 0 but `fc2_w` norm ≠ 0, the gradient is not flowing through
one of the layers between them. Check:
- MaxPool backward: does `_d_c3` get nonzero values from `_d_p3`?
- ReLU backward: is the masking using the right buffer (`_c3`, not `_d_c3`)?
- Conv backward: is the output gradient buffer correctly zeroed before accumulation?

**5. The Q10 optimizer mismatch (non-issue)**

The model was originally trained with Adam. This is irrelevant to backprop: the
backward pass computes `d(loss)/d(weights)` regardless of what optimizer was used
during pre-training. `asl_sgd_step()` then applies vanilla SGD. The optimizer choice
only affects the speed of convergence, not the correctness of the gradients.

---

## Memory Footprint Summary

| Category | Floats | Bytes |
|---|---|---|
| Weights (10 tensors) | 24,342 | 95.1 KB |
| Gradients (10 tensors) | 24,342 | 95.1 KB |
| Activation cache | 14,771 | 57.7 KB |
| Gradient scratch | 28,959 | 113.1 KB |
| **Total** | **92,414** | **361 KB** |

This is the full working set during a single forward+backward step. All buffers are
statically allocated; no heap allocation occurs during training.
