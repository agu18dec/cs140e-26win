// On-device training test.
//
// Two modes:
//   RANDOM_INIT=0 (default): load pretrained weights from MODEL.BIN on SD card.
//   RANDOM_INIT=1: skip SD card, initialize weights randomly, train from scratch.
//
// Either way: takes test image[0] from TESTD.BIN (or a synthetic image in random
// mode if NO_SD is set), runs 50 SGD steps, passes if loss drops by 50%.
//
// Build:
//   make INFERENCE=float TRAIN=1              # pretrained weights
//   make INFERENCE=float TRAIN=1 RANDOM_INIT=1  # random init (longer to converge)

#include "rpi.h"
#include "pi-sd.h"
#include "fat32.h"
#include "asl-train.h"

// Bare-metal sqrt via 3 Newton iterations (not in libgcc).
static float _sqrtf(float x) {
    if (x <= 0.0f) return 0.0f;
    union { float f; unsigned i; } u;
    u.f = x;
    u.i = (1u << 29) + (u.i >> 1) - (1u << 22); // bit-trick initial guess
    float y = u.f;
    y = 0.5f * (y + x / y);
    y = 0.5f * (y + x / y);
    y = 0.5f * (y + x / y);
    return y;
}

// Pi's printk doesn't support %f or zero-pad %0Nd.
// Build the 4-digit fraction string manually.
static void printk_float(const char *label, float v) {
    int neg = (v < 0.0f);
    if (neg) v = -v;
    int whole = (int)v;
    int frac  = (int)((v - (float)whole) * 10000.0f + 0.5f);
    if (frac >= 10000) { whole++; frac = 0; }
    // Build 4-digit zero-padded fraction string
    char fs[5];
    fs[4] = 0;
    fs[3] = '0' + (frac % 10); frac /= 10;
    fs[2] = '0' + (frac % 10); frac /= 10;
    fs[1] = '0' + (frac % 10); frac /= 10;
    fs[0] = '0' + (frac % 10);
    if (neg)
        printk("%s-%d.%s", label, whole, fs);
    else
        printk("%s%d.%s",  label, whole, fs);
}

#define TRAIN_STEPS  50
#define LR           0.01f

#ifndef RANDOM_INIT
#define RANDOM_INIT 0
#endif

void notmain(void) {
    kmalloc_init_mb(FAT32_HEAP_MB);

    asl_train_model_t model;

#if RANDOM_INIT
    // --- random init: no SD card needed for weights ---
    asl_train_random_init(&model, 0xC5140E42u);
    printk("Random init: weights initialized with He scaling\n");

    // Still need TESTD.BIN for a real image.
    // If you don't have it, we use a synthetic gradient-pattern image below.
    pi_sd_init();
    mbr_t *mbr = mbr_read();
    mbr_partition_ent_t part;
    memcpy(&part, mbr->part_tab1, sizeof(part));

    uint8_t img_buf[784];
    uint8_t *img;
    int true_label;

    if (mbr_part_is_fat32(part.part_type)) {
        fat32_fs_t fs = fat32_mk(&part);
        pi_dirent_t root = fat32_get_root(&fs);
        pi_file_t *tf = fat32_read(&fs, &root, "TESTD.BIN");
        if (tf) {
            uint8_t *p = (uint8_t *)tf->data + 4;  // skip n_images
            true_label = (int)*p++;
            img = p;
            printk("Using image[0] from TESTD.BIN, label=%c\n", asl_labels[true_label]);
        } else {
            // Fallback: synthetic image
            for (int i = 0; i < 784; i++) img_buf[i] = (uint8_t)(i % 256);
            img = img_buf;
            true_label = 0;
            printk("No TESTD.BIN — using synthetic image, label=A\n");
        }
    } else {
        for (int i = 0; i < 784; i++) img_buf[i] = (uint8_t)(i % 256);
        img = img_buf;
        true_label = 0;
        printk("No FAT32 — using synthetic image, label=A\n");
    }

#else
    // --- pretrained weights from SD card ---
    pi_sd_init();
    mbr_t *mbr = mbr_read();
    mbr_partition_ent_t part;
    memcpy(&part, mbr->part_tab1, sizeof(part));
    assert(mbr_part_is_fat32(part.part_type));

    fat32_fs_t fs = fat32_mk(&part);
    pi_dirent_t root = fat32_get_root(&fs);

    pi_file_t *wf = fat32_read(&fs, &root, "MODEL.BIN");
    demand(wf, "MODEL.BIN not found");
    demand(asl_train_load(&model, wf->data, wf->n_data), "bad model binary");
    printk("MODEL.BIN loaded: %d bytes\n", wf->n_data);

    pi_file_t *tf = fat32_read(&fs, &root, "TESTD.BIN");
    demand(tf, "TESTD.BIN not found");

    uint8_t *p = (uint8_t *)tf->data;
    uint32_t n_images = *(uint32_t *)p;  p += 4;
    demand(n_images >= 1, "empty test set");

    int true_label = (int)*p++;
    uint8_t *img   = p;
    printk("Using image[0]: true label=%c\n", asl_labels[true_label]);
#endif

    // --- initial loss + gradient norm check ---
    float init_loss = asl_train_forward(&model, img, true_label);
    printk_float("Initial loss: ", init_loss);
    printk("  (random baseline 3.1781)\n");

    asl_zero_grad(&model);
    asl_backward(&model, true_label);

    // Per-layer gradient norms (T3 diagnostic)
    static const int sz[10] = {72,8,1152,16,4608,32,18432,64,1536,24};
    static const char *nm[10] = {
        "conv1_w","conv1_b","conv2_w","conv2_b","conv3_w","conv3_b",
        "fc1_w","fc1_b","fc2_w","fc2_b"
    };
    float *gp[10] = {
        model.gconv1_w, model.gconv1_b,
        model.gconv2_w, model.gconv2_b,
        model.gconv3_w, model.gconv3_b,
        model.gfc1_w,   model.gfc1_b,
        model.gfc2_w,   model.gfc2_b,
    };
    printk("Gradient norms (first backward):\n");
    for (int i = 0; i < 10; i++) {
        float s = 0.0f;
        for (int j = 0; j < sz[i]; j++) s += gp[i][j] * gp[i][j];
        float norm = _sqrtf(s);
        printk("  %s ", nm[i]);
        printk_float("", norm);
        printk("\n");
    }

    // Apply the first SGD step (grad already computed above)
    asl_sgd_step(&model, LR);
    float loss = asl_train_forward(&model, img, true_label);
    printk("[0]  "); printk_float("loss=", loss); printk("\n");

    // --- main training loop ---
    for (int step = 1; step < TRAIN_STEPS; step++) {
        asl_zero_grad(&model);
        asl_train_forward(&model, img, true_label);
        asl_backward(&model, true_label);
        asl_sgd_step(&model, LR);
        loss = asl_train_forward(&model, img, true_label);
        if (step % 10 == 9 || step < 5) {
            printk("[%d]  ", step);
            printk_float("loss=", loss);
            printk("\n");
        }
    }

    // --- verdict ---
    float threshold = init_loss * 0.5f;
    if (loss < threshold) {
        printk_float("PASS: loss=", loss);
        printk_float(" < threshold=", threshold);
        printk("\n");
    } else {
        printk_float("FAIL: loss=", loss);
        printk_float(" >= threshold=", threshold);
        printk("\n");
    }
}
