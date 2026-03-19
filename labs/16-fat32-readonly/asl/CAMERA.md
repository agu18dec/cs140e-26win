# Connecting a Camera to the ASL Classifier

This document explains how to feed live camera frames into the on-device ASL model.
No camera driver code is provided here — this describes the interface contract and
the image preprocessing steps required.

---

## What the model expects

```c
float asl_train_forward(asl_train_model_t *m, const uint8_t *img, int true_label);
int   asl_train_argmax(void);   // call after forward to get predicted class index
```

`img` must be a flat `uint8_t[784]` array: a **28×28 grayscale** image in row-major
order, one byte per pixel, values in [0, 255].  The model normalises internally
(`pixel / 255.0f`).  The true_label is an index into `asl_labels[]` (defined in
`asl-cnn-float.h`); pass `-1` or any out-of-range value if you only want inference
and don't have a ground-truth label (the forward pass still computes logits, but the
loss returned will be meaningless).

---

## Preprocessing pipeline

Most cameras produce a colour frame at a much higher resolution.  You need to:

```
Camera frame  →  crop to square  →  resize to 28×28  →  RGB→grey  →  uint8[784]
```

### 1. Crop to square (centre crop)

ASL hand signs are roughly square.  Take a centre crop from the camera frame:

```
crop_size = min(frame_width, frame_height)
x0 = (frame_width  - crop_size) / 2
y0 = (frame_height - crop_size) / 2
```

### 2. Resize to 28×28 (nearest-neighbour is fine)

For each output pixel `(r, c)` in [0,28):

```
src_x = x0 + c * crop_size / 28
src_y = y0 + r * crop_size / 28
```

Nearest-neighbour (integer truncation) is fast and accurate enough at this resolution.

### 3. Convert to greyscale

The training data used standard luminance weighting:

```
grey = (77 * R + 150 * G + 29 * B) >> 8
```

(Equivalent to `0.299R + 0.587G + 0.114B`, integer-only arithmetic.)

### 4. Pack into uint8[784]

```c
uint8_t img[784];
for (int r = 0; r < 28; r++)
    for (int c = 0; c < 28; c++)
        img[r*28 + c] = grey_pixel(r, c);   // result of steps 1-3
```

---

## Calling the model

```c
// Inference only (no label known yet)
asl_train_forward(&m, img, 0);          // label arg ignored for inference
int pred = asl_train_argmax();
printk("Prediction: %c\n", asl_labels[pred]);

// Online learning (user confirms the correct letter)
int true_label = get_user_label();      // however you collect ground truth
float loss = asl_train_forward(&m, img, true_label);
asl_zero_grad(&m);
asl_train_forward(&m, img, true_label); // forward again to populate cache
asl_backward(&m, true_label);
asl_sgd_step(&m, 0.01f);               // lr=0.01, same as training runs
```

Note: `asl_train_forward` must be called once before `asl_backward` on the same
image because backprop reads the cached activations written by the forward pass.
Don't call `asl_zero_grad` between the two forward calls, only before `asl_backward`.

---

## Memory layout of the activation cache

The model keeps a single set of static activation buffers (~58 KB).  This means it
is **not re-entrant** — only one forward/backward pass can be in flight at a time.
For a bare-metal single-core Pi this is fine.

---

## Data format on SD card (TESTD.BIN / training sets)

If you want to save camera captures to the SD card for later batch training, use the
same binary format as `TESTD.BIN`:

```
[uint32  n_images]
[uint8   label_0][uint8 x 784  pixels_0]
[uint8   label_1][uint8 x 784  pixels_1]
...
```

Label is an index into `asl_labels[]` (`A`=0, `B`=1, …, `Y`=23, `J` and `Z` are
excluded because they require motion).  The existing `fat32_read` / `fat32_write`
infrastructure in `../code/` can be used to read and write this file from the Pi.

---

## Coordinate system for hand placement

The training data (`gen_test_data.py`) exports 28×28 centre-cropped greyscale images
from the Kaggle ASL alphabet dataset.  The hand occupies roughly the central 80% of
the frame.  For best accuracy:

- Hold your hand 20–40 cm from the camera
- Keep the hand centred in frame
- Use a plain background (the model is not robust to clutter at this resolution)
- Adequate lighting matters more than camera resolution — the final image is 28×28

---

## Relevant files

| File | Purpose |
|------|---------|
| `asl-train.h` | Model struct and API declarations |
| `asl-train.c` | Forward, backward, SGD — all training logic |
| `asl-cnn-float.h` | `asl_labels[]` character mapping |
| `asl-overfit20.c` | Example of loading images and calling the training loop |
| `BACKPROP.md` | Detailed explanation of the backpropagation implementation |
