// Overfit test: random weight init, 200 SGD steps on one image.
// Prints loss and predicted letter every step so we can plot the curve.
// Also prints gradient norms at step 0 to confirm gradient flows.
//
// Expected behaviour:
//   - loss starts near ln(24)=3.18 (random model)
//   - drops to <0.01 within ~50 steps at lr=0.01
//   - prediction flips to the correct letter within ~10 steps
//
// Build:  make INFERENCE=float OVERFIT=1
// Output: capture with  my-install ./asl-overfit.bin | tee pi_loss.txt

#include "rpi.h"
#include "pi-sd.h"
#include "fat32.h"
#include "asl-train.h"

#define STEPS  200
#define LR     0.01f

// Pi printk doesn't support %f or zero-padded %0Nd — build manually.
static void pf(float v) {
    int neg = (v < 0.0f);
    if (neg) v = -v;
    int w = (int)v;
    int f = (int)((v - (float)w) * 10000.0f + 0.5f);
    if (f >= 10000) { w++; f = 0; }
    char fs[5];
    fs[4]=0; fs[3]='0'+f%10; f/=10;
    fs[2]='0'+f%10; f/=10; fs[1]='0'+f%10; f/=10; fs[0]='0'+f%10;
    if (neg) printk("-%d.%s", w, fs);
    else     printk("%d.%s",  w, fs);
}

static float _sqrtf(float x) {
    if (x <= 0.0f) return 0.0f;
    union { float f; unsigned i; } u;
    u.f = x;
    u.i = (1u<<29) + (u.i>>1) - (1u<<22);
    float y = u.f;
    y = 0.5f*(y+x/y); y = 0.5f*(y+x/y); y = 0.5f*(y+x/y);
    return y;
}

void notmain(void) {
    kmalloc_init_mb(FAT32_HEAP_MB);

    // --- random weight init (no SD card needed for weights) ---
    asl_train_model_t m;
    asl_train_random_init(&m, 0xC5140E42u);
    printk("Random init done\n");

    // --- load one image from SD card ---
    pi_sd_init();
    mbr_t *mbr = mbr_read();
    mbr_partition_ent_t part;
    memcpy(&part, mbr->part_tab1, sizeof(part));

    uint8_t img_buf[784];
    uint8_t *img = img_buf;
    int label = 0;  // default: class A

    if (mbr_part_is_fat32(part.part_type)) {
        fat32_fs_t fs = fat32_mk(&part);
        pi_dirent_t root = fat32_get_root(&fs);
        pi_file_t *tf = fat32_read(&fs, &root, "TESTD.BIN");
        if (tf) {
            uint8_t *p = (uint8_t *)tf->data + 4;  // skip n_images uint32
            label = (int)*p++;
            img   = p;
            printk("Image loaded from SD, label=%c\n", asl_labels[label]);
        } else {
            for (int i = 0; i < 784; i++) img_buf[i] = (uint8_t)(i % 256);
            printk("No TESTD.BIN, using synthetic image, label=A\n");
        }
    } else {
        for (int i = 0; i < 784; i++) img_buf[i] = (uint8_t)(i % 256);
        printk("No FAT32, using synthetic image, label=A\n");
    }

    // --- step 0: forward + grad norms ---
    float loss = asl_train_forward(&m, img, label);
    int   pred = asl_train_argmax();

    asl_zero_grad(&m);
    asl_backward(&m, label);

    printk("--- grad norms at step 0 ---\n");
    static const int   gsz[10] = {72,8,1152,16,4608,32,18432,64,1536,24};
    static const char *gnm[10] = {
        "conv1_w","conv1_b","conv2_w","conv2_b","conv3_w","conv3_b",
        "fc1_w",  "fc1_b",  "fc2_w",  "fc2_b"
    };
    float *gp[10] = {
        m.gconv1_w, m.gconv1_b, m.gconv2_w, m.gconv2_b,
        m.gconv3_w, m.gconv3_b, m.gfc1_w,   m.gfc1_b,
        m.gfc2_w,   m.gfc2_b
    };
    for (int i = 0; i < 10; i++) {
        float s = 0;
        for (int j = 0; j < gsz[i]; j++) s += gp[i][j]*gp[i][j];
        printk("NORM %s ", gnm[i]); pf(_sqrtf(s)); printk("\n");
    }
    printk("--- training ---\n");

    // Print step 0 before first SGD step
    printk("STEP 0 loss="); pf(loss);
    printk(" pred=%c true=%c %s\n",
           asl_labels[pred], asl_labels[label],
           pred == label ? "CORRECT" : "wrong");

    asl_sgd_step(&m, LR);

    // --- main loop ---
    for (int step = 1; step <= STEPS; step++) {
        asl_zero_grad(&m);
        loss = asl_train_forward(&m, img, label);
        pred = asl_train_argmax();
        asl_backward(&m, label);
        asl_sgd_step(&m, LR);

        printk("STEP %d loss=", step); pf(loss);
        printk(" pred=%c true=%c %s\n",
               asl_labels[pred], asl_labels[label],
               pred == label ? "CORRECT" : "wrong");
    }

    printk("--- done ---\n");
    if (loss < 0.05f)
        printk("PASS: overfit successful\n");
    else
        printk("FAIL: loss still high\n");
}
