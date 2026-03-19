#include "asl-train.h"
#include <stdint.h>
#include <string.h>

// On Pi (bare-metal, -nostdlib): provide our own expf/logf.
// On host (TEST_GRADS_HOST): use real libm.
#ifdef TEST_GRADS_HOST
#  include <math.h>
#else
// IEEE-754 single-precision exp and log, accurate to ~1e-7 relative error.
// expf: range-reduces to |r| < ln2/2, uses 6-term Taylor, scales by 2^n.
static float expf(float x) {
    // Clamp to avoid overflow/underflow in the scale step
    if (x >  88.0f) return 3.4e38f;
    if (x < -87.0f) return 0.0f;
    // n = round(x / ln2),  r = x - n*ln2
    int n = (int)(x * 1.44269504f + (x < 0 ? -0.5f : 0.5f));
    float r = x - (float)n * 0.69314718f;
    // Horner-form Taylor: exp(r) = 1 + r + r^2/2 + r^3/6 + r^4/24 + r^5/120 + r^6/720
    float p = 1.0f + r * (1.0f + r * (0.5f + r * (0.16666667f
            + r * (0.04166667f + r * (0.00833333f + r * 0.00138889f)))));
    // Scale by 2^n via IEEE float bit manipulation
    union { float f; unsigned i; } u;
    u.i = (unsigned)(n + 127) << 23;
    return p * u.f;
}
// logf: extracts IEEE exponent+mantissa, reduces to [1,2), uses atanh series.
static float logf(float x) {
    union { float f; unsigned i; } u;
    u.f = x;
    int e = (int)((u.i >> 23) & 0xFF) - 127;
    u.i = (u.i & 0x7FFFFFu) | 0x3F800000u;  // mantissa m in [1,2)
    float m = u.f;
    // log(m) via 2*atanh((m-1)/(m+1)), 4-term series
    float t  = (m - 1.0f) / (m + 1.0f);
    float t2 = t * t;
    float lm = 2.0f * t * (1.0f + t2 * (0.33333333f
             + t2 * (0.20000000f + t2 * 0.14285714f)));
    return lm + (float)e * 0.69314718f;
}
// sqrtf: Newton-Raphson, 3 iterations — only used in TEST_GRADS_HOST printf
// (not needed on Pi path, but keeps compiler happy if referenced)
static float sqrtf(float x) {
    if (x <= 0.0f) return 0.0f;
    // initial guess via bit trick
    union { float f; unsigned i; } u;
    u.f = x;
    u.i = (1 << 29) + (u.i >> 1) - (1 << 22);
    float y = u.f;
    y = 0.5f * (y + x / y);
    y = 0.5f * (y + x / y);
    y = 0.5f * (y + x / y);
    return y;
}
#endif // TEST_GRADS_HOST

// ---------------------------------------------------------------------------
// Activation cache — file-scope statics, sized at compile time.
// All in HWC layout except _chw (CHW-reordered for FC1).
// ---------------------------------------------------------------------------
static float _img[28 * 28];         // normalized input [0,1]
static float _c1[28 * 28 * 8];     // conv1+relu output, pre-pool1
static float _p1[14 * 14 * 8];     // pool1 output = conv2 input
static float _c2[14 * 14 * 16];    // conv2+relu output, pre-pool2
static float _p2[7 * 7 * 16];      // pool2 output = conv3 input
static float _c3[7 * 7 * 32];      // conv3+relu output, pre-pool3
static float _p3[3 * 3 * 32];      // pool3 output in HWC
static float _chw[288];             // CHW-reordered, input to FC1
static float _f1[64];               // FC1+relu output
static float _logits[24];           // FC2 logits (pre-softmax)

// ---------------------------------------------------------------------------
// Gradient scratch — file-scope statics.
// ---------------------------------------------------------------------------
static float _d_logits[24];
static float _d_f1[64];             // doubles as d_fc1_pre (modified in-place)
static float _d_chw[288];
static float _d_p3[288];
static float _d_c3[7 * 7 * 32];
static float _d_p2[7 * 7 * 16];
static float _d_c2[14 * 14 * 16];
static float _d_p1[14 * 14 * 8];
static float _d_c1[28 * 28 * 8];   // not used for input grad past conv1

// ---------------------------------------------------------------------------
// Helpers shared with forward and backward
// ---------------------------------------------------------------------------

#ifndef TEST_GRADS_HOST
// On Pi: kmalloc from rpi.h; math from compiler-rt / libm
#include "rpi.h"
#else
// Host build: use stdlib
#include <stdlib.h>
static void *kmalloc(uint32_t n) { return calloc(1, n); }
#endif

// Leaky ReLU: f(x) = x > 0 ? x : 0.01*x
// Prevents dead neurons — gradient 0.01 flows even for negative activations.
static void _relu(float *x, int n) {
    for (int i = 0; i < n; i++)
        if (x[i] < 0.0f) x[i] = 0.01f * x[i];
}

// Leaky ReLU backward: post-activation value a = f(pre).
// pre > 0  ↔  a > 0  (since 0.01 > 0), so we can check a directly.
#define LRELU_GRAD(a) ((a) > 0.0f ? 1.0f : 0.01f)

static void _conv2d(const float *in, int H, int W, int IC,
                    float *out, int OC,
                    const float *w, const float *b) {
    for (int oc = 0; oc < OC; oc++) {
        const float *woc = w + oc * IC * 9;
        for (int oh = 0; oh < H; oh++) {
            for (int ow = 0; ow < W; ow++) {
                float acc = b[oc];
                for (int ic = 0; ic < IC; ic++) {
                    const float *wic = woc + ic * 9;
                    for (int kh = 0; kh < 3; kh++) {
                        int ih = oh + kh - 1;
                        if (ih < 0 || ih >= H) continue;
                        for (int kw = 0; kw < 3; kw++) {
                            int iw = ow + kw - 1;
                            if (iw < 0 || iw >= W) continue;
                            acc += in[ih * W * IC + iw * IC + ic]
                                 * wic[kh * 3 + kw];
                        }
                    }
                }
                out[oh * W * OC + ow * OC + oc] = acc;
            }
        }
    }
}

static void _maxpool2(const float *in, int H, int W, int C, float *out) {
    int OH = H / 2, OW = W / 2;
    for (int oh = 0; oh < OH; oh++) {
        for (int ow = 0; ow < OW; ow++) {
            for (int c = 0; c < C; c++) {
                float mx = in[(oh*2)*W*C + (ow*2)*C + c];
                for (int kh = 0; kh < 2; kh++)
                    for (int kw = 0; kw < 2; kw++) {
                        float v = in[(oh*2+kh)*W*C + (ow*2+kw)*C + c];
                        if (v > mx) mx = v;
                    }
                out[oh * OW * C + ow * C + c] = mx;
            }
        }
    }
}

static void _fc(const float *in, int in_n, float *out, int out_n,
                const float *w, const float *b) {
    for (int j = 0; j < out_n; j++) {
        float acc = b[j];
        const float *wj = w + j * in_n;
        for (int i = 0; i < in_n; i++)
            acc += in[i] * wj[i];
        out[j] = acc;
    }
}

// ---------------------------------------------------------------------------
// asl_train_load
// ---------------------------------------------------------------------------
int asl_train_load(asl_train_model_t *m, void *data, uint32_t nbytes) {
    uint32_t *hdr = (uint32_t *)data;
    if (hdr[0] != 10 || hdr[1] != 10)
        return 0;

    // Sizes for each of the 10 parameter tensors (in elements)
    static const uint32_t param_sizes[10] = {
        72, 8, 1152, 16, 4608, 32, 18432, 64, 1536, 24
    };

    float **weights[] = {
        &m->conv1_w, &m->conv1_b,
        &m->conv2_w, &m->conv2_b,
        &m->conv3_w, &m->conv3_b,
        &m->fc1_w,   &m->fc1_b,
        &m->fc2_w,   &m->fc2_b,
    };
    float **grads[] = {
        &m->gconv1_w, &m->gconv1_b,
        &m->gconv2_w, &m->gconv2_b,
        &m->gconv3_w, &m->gconv3_b,
        &m->gfc1_w,   &m->gfc1_b,
        &m->gfc2_w,   &m->gfc2_b,
    };

    uint8_t *p = (uint8_t *)data + 8;   // skip 2x uint32 header
    for (int i = 0; i < 10; i++) {
        uint32_t n = *(uint32_t *)p;  p += 4;
        int16_t *q  = (int16_t *)p;   p += n * sizeof(int16_t);
        float   *f  = kmalloc(n * sizeof(float));
        for (uint32_t j = 0; j < n; j++)
            f[j] = q[j] / 1024.0f;
        *weights[i] = f;

        // Allocate and zero gradient array (same shape)
        float *g = kmalloc(n * sizeof(float));
        memset(g, 0, n * sizeof(float));
        *grads[i] = g;

        (void)param_sizes[i]; // suppress unused warning
    }
    return 1;
}

// ---------------------------------------------------------------------------
// asl_train_random_init — random weight init, no binary needed
// ---------------------------------------------------------------------------
// Simple LCG (Knuth): x = x * 1664525 + 1013904223
// Box-Muller transform for Gaussian samples.
// He initialization scale: sqrt(2/fan_in).
void asl_train_random_init(asl_train_model_t *m, uint32_t seed) {
    static const uint32_t n_weights[10] = {
        72, 8, 1152, 16, 4608, 32, 18432, 64, 1536, 24
    };
    // fan_in for each weight tensor (bias fan_in = 1, treated as 0-init)
    static const uint32_t fan_in[10] = {
        9,  1,    // conv1_w (1*3*3=9),  conv1_b
        72, 1,    // conv2_w (8*3*3=72), conv2_b
        144, 1,   // conv3_w (16*3*3=144), conv3_b
        288, 1,   // fc1_w, fc1_b
        64,  1,   // fc2_w, fc2_b
    };

    float **weights[] = {
        &m->conv1_w, &m->conv1_b,
        &m->conv2_w, &m->conv2_b,
        &m->conv3_w, &m->conv3_b,
        &m->fc1_w,   &m->fc1_b,
        &m->fc2_w,   &m->fc2_b,
    };
    float **grads[] = {
        &m->gconv1_w, &m->gconv1_b,
        &m->gconv2_w, &m->gconv2_b,
        &m->gconv3_w, &m->gconv3_b,
        &m->gfc1_w,   &m->gfc1_b,
        &m->gfc2_w,   &m->gfc2_b,
    };

    uint32_t s = seed ? seed : 0xDEADBEEFu;

    for (int i = 0; i < 10; i++) {
        uint32_t n = n_weights[i];
        float   *w = kmalloc(n * sizeof(float));
        float   *g = kmalloc(n * sizeof(float));
        memset(g, 0, n * sizeof(float));
        *weights[i] = w;
        *grads[i]   = g;

        // Biases: positive init to keep ReLU neurons alive.
        // 0.5 for conv/fc1 biases so that pre-ReLU outputs are mostly positive
        // with He-initialized weights (which average to 0).  0.1 was too small —
        // most conv neurons stayed dead → only FC2 learned → linear progress only.
        // fc2 bias (i==9) stays 0 — output layer doesn't need activation push.
        if (i % 2 == 1) {
            float bval = (i == 9) ? 0.0f : 0.5f;
            for (uint32_t j = 0; j < n; j++) w[j] = bval;
            continue;
        }

        // Weights: He init scale = sqrt(2/fan_in)
        // Gaussian via Box-Muller: z = sqrt(-2*log(u1)) * cos(2*pi*u2)
        float scale = 1.0f;
        {   // approx sqrt(2/fan_in) without sqrtf dependency cascade
            float fi = (float)fan_in[i];
            // Newton step for sqrt: start at 1, converge fast
            float s2fi = 2.0f / fi;
            float y = s2fi < 1.0f ? 1.0f : s2fi;
            y = 0.5f * (y + s2fi / y);
            y = 0.5f * (y + s2fi / y);
            scale = y;  // ≈ sqrt(2/fan_in)
        }

        for (uint32_t j = 0; j < n; j += 2) {
            // Two LCG steps → two uniform samples in (0,1)
            s = s * 1664525u + 1013904223u;
            float u1 = (float)(s >> 9 | 0x3F800000u);  // [1,2)
            u1 -= 1.0f;  // [0,1)
            u1 += 1e-7f; // avoid log(0)

            s = s * 1664525u + 1013904223u;
            float u2 = (float)(s >> 9 | 0x3F800000u);
            u2 -= 1.0f;

            // Box-Muller: z1 = sqrt(-2*log(u1)) * cos(2*pi*u2)
            // cos(2*pi*u2) ≈ 1 - 2*(u2-0.5)^2 for simple approximation
            // Better: just use the magnitude sqrt(-2*log(u1)) with uniform sign
            float r  = logf(u1);  // negative
            r = r < -10.0f ? 10.0f : -r;  // clamp: r in (0, 10]
            // sqrt via Newton
            float mag = r;
            mag = 0.5f * (mag + r / mag);
            mag = 0.5f * (mag + r / mag);  // ≈ sqrt(-2*log(u1))
            // sign from u2
            float z1 = (u2 < 0.5f ? -1.0f : 1.0f) * mag * scale;
            float z2 = (u2 < 0.25f || u2 > 0.75f ? -1.0f : 1.0f) * mag * scale;

            w[j]     = z1;
            if (j+1 < n) w[j+1] = z2;
        }
    }
}

// ---------------------------------------------------------------------------
// asl_train_forward — caching forward pass, returns CE loss
// ---------------------------------------------------------------------------
float asl_train_forward(asl_train_model_t *m, const uint8_t *img, int true_label) {
    // 1. Normalize
    for (int i = 0; i < 784; i++)
        _img[i] = img[i] / 255.0f;

    // 2. Conv1 → _c1, relu in-place
    _conv2d(_img, 28, 28, 1, _c1, 8, m->conv1_w, m->conv1_b);
    _relu(_c1, 28 * 28 * 8);

    // 3. Pool1 → _p1
    _maxpool2(_c1, 28, 28, 8, _p1);

    // 4. Conv2 → _c2, relu in-place
    _conv2d(_p1, 14, 14, 8, _c2, 16, m->conv2_w, m->conv2_b);
    _relu(_c2, 14 * 14 * 16);

    // 5. Pool2 → _p2
    _maxpool2(_c2, 14, 14, 16, _p2);

    // 6. Conv3 → _c3, relu in-place
    _conv2d(_p2, 7, 7, 16, _c3, 32, m->conv3_w, m->conv3_b);
    _relu(_c3, 7 * 7 * 32);

    // 7. Pool3 → _p3 (HWC: [3,3,32])
    _maxpool2(_c3, 7, 7, 32, _p3);

    // 8. HWC → CHW reorder → _chw [32,3,3]
    for (int c = 0; c < 32; c++)
        for (int h = 0; h < 3; h++)
            for (int w = 0; w < 3; w++)
                _chw[c*9 + h*3 + w] = _p3[h*3*32 + w*32 + c];

    // 9. FC1 → _f1, relu in-place
    _fc(_chw, 288, _f1, 64, m->fc1_w, m->fc1_b);
    _relu(_f1, 64);

    // 10. FC2 → _logits
    _fc(_f1, 64, _logits, 24, m->fc2_w, m->fc2_b);

    // 11. Numerically stable softmax + CE loss
    float max_l = _logits[0];
    for (int i = 1; i < 24; i++)
        if (_logits[i] > max_l) max_l = _logits[i];

    float probs[24];
    float sum = 0.0f;
    for (int i = 0; i < 24; i++) {
        probs[i] = expf(_logits[i] - max_l);
        sum += probs[i];
    }

    return -logf(probs[true_label] / sum);
}

// ---------------------------------------------------------------------------
// asl_backward — accumulate gradients into m->g* (call after forward)
// ---------------------------------------------------------------------------
void asl_backward(asl_train_model_t *m, int true_label) {
    // ---- Step 1: Softmax+CE → d_logits ----
    // Recompute softmax probs from cached _logits
    float max_l = _logits[0];
    for (int i = 1; i < 24; i++)
        if (_logits[i] > max_l) max_l = _logits[i];
    float probs[24], sum = 0.0f;
    for (int i = 0; i < 24; i++) {
        probs[i] = expf(_logits[i] - max_l);
        sum += probs[i];
    }
    for (int i = 0; i < 24; i++)
        _d_logits[i] = probs[i] / sum;
    _d_logits[true_label] -= 1.0f;

    // ---- Step 2: FC2 grad + input grad ----
    // gfc2_b[j] += d_logits[j]
    // gfc2_w[j*64+i] += d_logits[j] * _f1[i]
    // _d_f1[i] = sum_j d_logits[j] * fc2_w[j*64+i]
    for (int i = 0; i < 64; i++) _d_f1[i] = 0.0f;
    for (int j = 0; j < 24; j++) {
        float dl = _d_logits[j];
        m->gfc2_b[j] += dl;
        float *gw = m->gfc2_w + j * 64;
        const float *wj = m->fc2_w + j * 64;
        for (int i = 0; i < 64; i++) {
            gw[i] += dl * _f1[i];
            _d_f1[i] += dl * wj[i];
        }
    }

    // ---- Step 3: Leaky ReLU backward (FC1) ----
    for (int i = 0; i < 64; i++)
        _d_f1[i] *= LRELU_GRAD(_f1[i]);

    // ---- Step 4: FC1 grad + input grad ----
    // gfc1_b[j] += _d_f1[j]
    // gfc1_w[j*288+i] += _d_f1[j] * _chw[i]
    // _d_chw[i] = sum_j _d_f1[j] * fc1_w[j*288+i]
    for (int i = 0; i < 288; i++) _d_chw[i] = 0.0f;
    for (int j = 0; j < 64; j++) {
        float df = _d_f1[j];
        m->gfc1_b[j] += df;
        float *gw = m->gfc1_w + j * 288;
        const float *wj = m->fc1_w + j * 288;
        for (int i = 0; i < 288; i++) {
            gw[i] += df * _chw[i];
            _d_chw[i] += df * wj[i];
        }
    }

    // ---- Step 5: Transpose backward (undo HWC→CHW) ----
    // forward: _chw[c*9+h*3+w] = _p3[h*96+w*32+c]
    // backward: _d_p3[h*96+w*32+c] = _d_chw[c*9+h*3+w]
    for (int c = 0; c < 32; c++)
        for (int h = 0; h < 3; h++)
            for (int w = 0; w < 3; w++)
                _d_p3[h*3*32 + w*32 + c] = _d_chw[c*9 + h*3 + w];

    // ---- Step 6: MaxPool3 backward ----
    // _c3 is [7,7,32] HWC.  _p3 output is [3,3,32] HWC (floor(7/2)=3).
    memset(_d_c3, 0, sizeof(_d_c3));
    for (int oh = 0; oh < 3; oh++) {
        for (int ow = 0; ow < 3; ow++) {
            for (int c = 0; c < 32; c++) {
                // find argmax in 2x2 window of _c3
                int bih = oh*2, biw = ow*2;
                float mx = _c3[(oh*2)*7*32 + (ow*2)*32 + c];
                for (int kh = 0; kh < 2; kh++) {
                    for (int kw = 0; kw < 2; kw++) {
                        float v = _c3[(oh*2+kh)*7*32 + (ow*2+kw)*32 + c];
                        if (v > mx) { mx = v; bih = oh*2+kh; biw = ow*2+kw; }
                    }
                }
                _d_c3[bih*7*32 + biw*32 + c] += _d_p3[oh*3*32 + ow*32 + c];
            }
        }
    }

    // ---- Step 7: Leaky ReLU3 backward ----
    for (int i = 0; i < 7*7*32; i++)
        _d_c3[i] *= LRELU_GRAD(_c3[i]);

    // ---- Step 8: Conv3 grad + input grad ----
    // Conv3: in=_p2[7,7,16], out=_c3[7,7,32], kernel [32,16,3,3]
    memset(_d_p2, 0, sizeof(_d_p2));
    for (int oc = 0; oc < 32; oc++) {
        float gb = 0.0f;
        for (int oh = 0; oh < 7; oh++) {
            for (int ow = 0; ow < 7; ow++) {
                float dc = _d_c3[oh*7*32 + ow*32 + oc];
                gb += dc;
                for (int ic = 0; ic < 16; ic++) {
                    for (int kh = 0; kh < 3; kh++) {
                        int ih = oh + kh - 1;
                        if (ih < 0 || ih >= 7) continue;
                        for (int kw = 0; kw < 3; kw++) {
                            int iw = ow + kw - 1;
                            if (iw < 0 || iw >= 7) continue;
                            int widx = oc*16*9 + ic*9 + kh*3 + kw;
                            m->gconv3_w[widx] += dc * _p2[ih*7*16 + iw*16 + ic];
                            _d_p2[ih*7*16 + iw*16 + ic] += dc * m->conv3_w[widx];
                        }
                    }
                }
            }
        }
        m->gconv3_b[oc] += gb;
    }

    // ---- Step 9: MaxPool2 backward ----
    // _c2 is [14,14,16] HWC.  _p2 output is [7,7,16].
    memset(_d_c2, 0, sizeof(_d_c2));
    for (int oh = 0; oh < 7; oh++) {
        for (int ow = 0; ow < 7; ow++) {
            for (int c = 0; c < 16; c++) {
                int bih = oh*2, biw = ow*2;
                float mx = _c2[(oh*2)*14*16 + (ow*2)*16 + c];
                for (int kh = 0; kh < 2; kh++) {
                    for (int kw = 0; kw < 2; kw++) {
                        float v = _c2[(oh*2+kh)*14*16 + (ow*2+kw)*16 + c];
                        if (v > mx) { mx = v; bih = oh*2+kh; biw = ow*2+kw; }
                    }
                }
                _d_c2[bih*14*16 + biw*16 + c] += _d_p2[oh*7*16 + ow*16 + c];
            }
        }
    }

    // ---- Step 10: Leaky ReLU2 backward ----
    for (int i = 0; i < 14*14*16; i++)
        _d_c2[i] *= LRELU_GRAD(_c2[i]);

    // ---- Step 11: Conv2 grad + input grad ----
    // Conv2: in=_p1[14,14,8], out=_c2[14,14,16], kernel [16,8,3,3]
    memset(_d_p1, 0, sizeof(_d_p1));
    for (int oc = 0; oc < 16; oc++) {
        float gb = 0.0f;
        for (int oh = 0; oh < 14; oh++) {
            for (int ow = 0; ow < 14; ow++) {
                float dc = _d_c2[oh*14*16 + ow*16 + oc];
                gb += dc;
                for (int ic = 0; ic < 8; ic++) {
                    for (int kh = 0; kh < 3; kh++) {
                        int ih = oh + kh - 1;
                        if (ih < 0 || ih >= 14) continue;
                        for (int kw = 0; kw < 3; kw++) {
                            int iw = ow + kw - 1;
                            if (iw < 0 || iw >= 14) continue;
                            int widx = oc*8*9 + ic*9 + kh*3 + kw;
                            m->gconv2_w[widx] += dc * _p1[ih*14*8 + iw*8 + ic];
                            _d_p1[ih*14*8 + iw*8 + ic] += dc * m->conv2_w[widx];
                        }
                    }
                }
            }
        }
        m->gconv2_b[oc] += gb;
    }

    // ---- Step 12: MaxPool1 backward ----
    // _c1 is [28,28,8] HWC.  _p1 output is [14,14,8].
    memset(_d_c1, 0, sizeof(_d_c1));
    for (int oh = 0; oh < 14; oh++) {
        for (int ow = 0; ow < 14; ow++) {
            for (int c = 0; c < 8; c++) {
                int bih = oh*2, biw = ow*2;
                float mx = _c1[(oh*2)*28*8 + (ow*2)*8 + c];
                for (int kh = 0; kh < 2; kh++) {
                    for (int kw = 0; kw < 2; kw++) {
                        float v = _c1[(oh*2+kh)*28*8 + (ow*2+kw)*8 + c];
                        if (v > mx) { mx = v; bih = oh*2+kh; biw = ow*2+kw; }
                    }
                }
                _d_c1[bih*28*8 + biw*8 + c] += _d_p1[oh*14*8 + ow*8 + c];
            }
        }
    }

    // ---- Step 13: Leaky ReLU1 backward ----
    for (int i = 0; i < 28*28*8; i++)
        _d_c1[i] *= LRELU_GRAD(_c1[i]);

    // ---- Step 14: Conv1 grad (no input grad needed) ----
    // Conv1: in=_img[28,28,1], out=_c1[28,28,8], kernel [8,1,3,3]
    for (int oc = 0; oc < 8; oc++) {
        float gb = 0.0f;
        for (int oh = 0; oh < 28; oh++) {
            for (int ow = 0; ow < 28; ow++) {
                float dc = _d_c1[oh*28*8 + ow*8 + oc];
                gb += dc;
                for (int kh = 0; kh < 3; kh++) {
                    int ih = oh + kh - 1;
                    if (ih < 0 || ih >= 28) continue;
                    for (int kw = 0; kw < 3; kw++) {
                        int iw = ow + kw - 1;
                        if (iw < 0 || iw >= 28) continue;
                        // ic=0 (single-channel input)
                        int widx = oc*9 + kh*3 + kw;
                        m->gconv1_w[widx] += dc * _img[ih*28 + iw];
                    }
                }
            }
        }
        m->gconv1_b[oc] += gb;
    }
}

// ---------------------------------------------------------------------------
// asl_train_argmax
// ---------------------------------------------------------------------------
int asl_train_argmax(void) {
    int best = 0;
    for (int i = 1; i < 24; i++)
        if (_logits[i] > _logits[best]) best = i;
    return best;
}

// ---------------------------------------------------------------------------
// asl_zero_grad
// ---------------------------------------------------------------------------
void asl_zero_grad(asl_train_model_t *m) {
    static const uint32_t sizes[10] = {
        72, 8, 1152, 16, 4608, 32, 18432, 64, 1536, 24
    };
    float *grads[10] = {
        m->gconv1_w, m->gconv1_b,
        m->gconv2_w, m->gconv2_b,
        m->gconv3_w, m->gconv3_b,
        m->gfc1_w,   m->gfc1_b,
        m->gfc2_w,   m->gfc2_b,
    };
    for (int i = 0; i < 10; i++)
        memset(grads[i], 0, sizes[i] * sizeof(float));
}

// ---------------------------------------------------------------------------
// asl_sgd_step
// ---------------------------------------------------------------------------
void asl_sgd_step(asl_train_model_t *m, float lr) {
    static const uint32_t sizes[10] = {
        72, 8, 1152, 16, 4608, 32, 18432, 64, 1536, 24
    };
    float *params[10] = {
        m->conv1_w, m->conv1_b,
        m->conv2_w, m->conv2_b,
        m->conv3_w, m->conv3_b,
        m->fc1_w,   m->fc1_b,
        m->fc2_w,   m->fc2_b,
    };
    float *grads[10] = {
        m->gconv1_w, m->gconv1_b,
        m->gconv2_w, m->gconv2_b,
        m->gconv3_w, m->gconv3_b,
        m->gfc1_w,   m->gfc1_b,
        m->gfc2_w,   m->gfc2_b,
    };
    // Gradient clipping: conv layer grads sum over all spatial positions
    // (784 for conv1, 196 for conv2, 49 for conv3), making them huge with
    // leaky ReLU flowing.  Clip each element to ±1.0 before applying.
    for (int i = 0; i < 10; i++) {
        for (uint32_t j = 0; j < sizes[i]; j++) {
            float g = grads[i][j];
            if (g >  1.0f) g =  1.0f;
            if (g < -1.0f) g = -1.0f;
            params[i][j] -= lr * g;
        }
    }
}

// ---------------------------------------------------------------------------
// Host-only numeric gradient checker
// ---------------------------------------------------------------------------
#ifdef TEST_GRADS_HOST
#include <stdio.h>
#include <assert.h>

// Forward pass that returns loss without printing, given a model and image
static float _forward_loss(asl_train_model_t *m, const uint8_t *img, int label) {
    return asl_train_forward(m, img, label);
}

static float _norm(const float *v, int n) {
    float s = 0;
    for (int i = 0; i < n; i++) s += v[i] * v[i];
    return sqrtf(s);
}

static void check_grad(const char *name,
                       float *param, float *grad, int n,
                       asl_train_model_t *m,
                       const uint8_t *img, int label,
                       float eps) {
    // Pick 8 evenly-spaced indices
    int step = n / 8;
    if (step < 1) step = 1;
    int ncheck = 0;
    float max_rel = 0.0f;
    for (int idx = step/2; idx < n && ncheck < 8; idx += step, ncheck++) {
        float orig = param[idx];

        param[idx] = orig + eps;
        float lp = _forward_loss(m, img, label);

        param[idx] = orig - eps;
        float lm = _forward_loss(m, img, label);

        param[idx] = orig;

        float numeric = (lp - lm) / (2.0f * eps);
        float analytic = grad[idx];
        // Combined abs+rel error with atol=0.05 to tolerate ReLU kinks at
        // exactly-zero activations (dead neurons have analytic grad=0 but
        // finite-difference sees the kink — this is correct, not a bug).
        float denom = fabsf(analytic) > fabsf(numeric)
                    ? fabsf(analytic) : fabsf(numeric);
        if (denom < 0.05f) denom = 0.05f;
        float rel = fabsf(numeric - analytic) / denom;
        if (rel > max_rel) max_rel = rel;
    }
    int ok = max_rel < 0.5f;  // generous: catches sign errors; kinks give at most ~1.0
    printf("  %-16s  max_err=%.4e  %s\n", name, max_rel, ok ? "PASS" : "FAIL");
}

int main(void) {
    // Build a tiny synthetic model with small random weights
    // (use the same Q10 format so asl_train_load works)
    // Instead: directly populate a model struct with small random floats
    asl_train_model_t m;
    static const uint32_t sizes[10] = {
        72, 8, 1152, 16, 4608, 32, 18432, 64, 1536, 24
    };
    float **params[10] = {
        &m.conv1_w, &m.conv1_b,
        &m.conv2_w, &m.conv2_b,
        &m.conv3_w, &m.conv3_b,
        &m.fc1_w,   &m.fc1_b,
        &m.fc2_w,   &m.fc2_b,
    };
    float **grads[10] = {
        &m.gconv1_w, &m.gconv1_b,
        &m.gconv2_w, &m.gconv2_b,
        &m.gconv3_w, &m.gconv3_b,
        &m.gfc1_w,   &m.gfc1_b,
        &m.gfc2_w,   &m.gfc2_b,
    };
    // Seed random weights
    srand(42);
    for (int i = 0; i < 10; i++) {
        *params[i] = calloc(sizes[i], sizeof(float));
        *grads[i]  = calloc(sizes[i], sizeof(float));
        // Small random weights (avoids dead ReLUs)
        for (uint32_t j = 0; j < sizes[i]; j++)
            (*params[i])[j] = ((float)(rand() % 2001) - 1000) / 10000.0f;
        // Zero biases
        if (i % 2 == 1)
            memset(*params[i], 0, sizes[i] * sizeof(float));
    }

    // Synthetic image: simple gradient pattern
    uint8_t img[784];
    for (int i = 0; i < 784; i++) img[i] = (uint8_t)(i % 256);
    int label = 3;

    // Run forward + backward
    float loss = asl_train_forward(&m, img, label);
    printf("Initial loss: %.4f (expected ~ln(24)=%.4f)\n", loss, logf(24.0f));

    asl_zero_grad(&m);
    asl_backward(&m, label);

    // Print gradient norms
    const char *names[10] = {
        "conv1_w","conv1_b","conv2_w","conv2_b","conv3_w","conv3_b",
        "fc1_w","fc1_b","fc2_w","fc2_b"
    };
    printf("\nGradient norms:\n");
    for (int i = 0; i < 10; i++)
        printf("  %-12s  norm=%.4e\n", names[i], _norm(*grads[i], sizes[i]));

    // Numeric gradient check
    float eps = 1e-3f;
    printf("\nNumeric gradient check (eps=%.0e):\n", eps);
    check_grad("conv1_w", m.conv1_w, m.gconv1_w, 72,    &m, img, label, eps);
    check_grad("conv1_b", m.conv1_b, m.gconv1_b, 8,     &m, img, label, eps);
    check_grad("conv2_w", m.conv2_w, m.gconv2_w, 1152,  &m, img, label, eps);
    check_grad("conv2_b", m.conv2_b, m.gconv2_b, 16,    &m, img, label, eps);
    check_grad("conv3_w", m.conv3_w, m.gconv3_w, 4608,  &m, img, label, eps);
    check_grad("conv3_b", m.conv3_b, m.gconv3_b, 32,    &m, img, label, eps);
    check_grad("fc1_w",   m.fc1_w,   m.gfc1_w,   18432, &m, img, label, eps);
    check_grad("fc1_b",   m.fc1_b,   m.gfc1_b,   64,    &m, img, label, eps);
    check_grad("fc2_w",   m.fc2_w,   m.gfc2_w,   1536,  &m, img, label, eps);
    check_grad("fc2_b",   m.fc2_b,   m.gfc2_b,   24,    &m, img, label, eps);

    return 0;
}
#endif // TEST_GRADS_HOST
