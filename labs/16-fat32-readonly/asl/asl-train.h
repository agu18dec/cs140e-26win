#pragma once
#include <stdint.h>

// TinyCNN float32 training: weights + gradients, forward+backward+SGD.
// Loaded from the same Q10 binary as asl-cnn-float.h.

#define ASL_NUM_CLASSES  24
#define ASL_IMG_PIXELS   (28 * 28)

extern const char asl_labels[ASL_NUM_CLASSES];

typedef struct {
    // Weights (dequantized from Q10 binary)
    float *conv1_w, *conv1_b;   // [8,1,3,3]=72,    [8]
    float *conv2_w, *conv2_b;   // [16,8,3,3]=1152,  [16]
    float *conv3_w, *conv3_b;   // [32,16,3,3]=4608, [32]
    float *fc1_w,   *fc1_b;     // [64,288]=18432,   [64]
    float *fc2_w,   *fc2_b;     // [24,64]=1536,     [24]

    // Gradients (same shapes, zeroed on init)
    float *gconv1_w, *gconv1_b;
    float *gconv2_w, *gconv2_b;
    float *gconv3_w, *gconv3_b;
    float *gfc1_w,   *gfc1_b;
    float *gfc2_w,   *gfc2_b;
} asl_train_model_t;

// Load Q10 binary, dequantize weights, allocate+zero gradient arrays.
// Returns 1 on success, 0 on format error.
int   asl_train_load(asl_train_model_t *m, void *data, uint32_t nbytes);

// Initialize all weights randomly (small Gaussian, He-style scale).
// seed: any nonzero uint32 — same seed gives same weights.
// Use this to start training from scratch without a pretrained binary.
void  asl_train_random_init(asl_train_model_t *m, uint32_t seed);

// Forward pass with full activation caching.  Returns cross-entropy loss.
float asl_train_forward(asl_train_model_t *m, const uint8_t *img, int true_label);

// Backward pass: accumulates gradients into m->g* arrays.
// Must call asl_train_forward() first to populate activation caches.
void  asl_backward(asl_train_model_t *m, int true_label);

// Zero all gradient arrays (call before each batch).
void  asl_zero_grad(asl_train_model_t *m);

// SGD update: param -= lr * grad, for all 10 parameter tensors.
void  asl_sgd_step(asl_train_model_t *m, float lr);

// Returns the predicted class (argmax of logits) from the most recent
// asl_train_forward() call.  Call immediately after forward, before any
// other forward pass overwrites the logit cache.
int   asl_train_argmax(void);
