// Overfit on 20 samples from TESTD.BIN.
// Each epoch: SGD update on all N_SAMPLES images in sequence.
// After each epoch: compute loss + accuracy over all N_SAMPLES.
// Prints one line per epoch:  EPOCH n loss=X.XXXX acc=k/20
//
// Build:  make INFERENCE=float OVERFIT20=1
// Run:    my-install ./asl-overfit20.bin | tee pi_loss20.txt

#include "rpi.h"
#include "pi-sd.h"
#include "fat32.h"
#include "asl-train.h"

#define N_SAMPLES  20
#define EPOCHS     50
#define LR         0.01f

static float _sqrtf(float x) {
    if (x <= 0.0f) return 0.0f;
    union { float f; unsigned i; } u;
    u.f = x; u.i = (1u<<29)+(u.i>>1)-(1u<<22);
    float y=u.f; y=0.5f*(y+x/y); y=0.5f*(y+x/y); y=0.5f*(y+x/y);
    return y;
}

static void pf(float v) {
    int neg = v < 0.0f; if (neg) v=-v;
    int w=(int)v, f=(int)((v-(float)w)*10000.0f+0.5f);
    if(f>=10000){w++;f=0;}
    char s[5]; s[4]=0;
    s[3]='0'+f%10;f/=10; s[2]='0'+f%10;f/=10;
    s[1]='0'+f%10;f/=10; s[0]='0'+f%10;
    if(neg) printk("-%d.%s",w,s); else printk("%d.%s",w,s);
}

void notmain(void) {
    kmalloc_init_mb(FAT32_HEAP_MB);

    // Random init
    asl_train_model_t m;
    asl_train_random_init(&m, 0xC5140E42u);
    printk("Random init done\n");

    // Load images from SD
    pi_sd_init();
    mbr_t *mbr = mbr_read();
    mbr_partition_ent_t part;
    memcpy(&part, mbr->part_tab1, sizeof(part));
    demand(mbr_part_is_fat32(part.part_type), "need FAT32");

    fat32_fs_t fs = fat32_mk(&part);
    pi_dirent_t root = fat32_get_root(&fs);
    pi_file_t *tf = fat32_read(&fs, &root, "TESTD.BIN");
    demand(tf, "TESTD.BIN not found");

    uint8_t *base = (uint8_t *)tf->data;
    uint32_t total = *(uint32_t*)base;
    int n = (total < N_SAMPLES) ? (int)total : N_SAMPLES;
    printk("Loaded %d images (using first %d)\n", total, n);

    // Pack images + labels into flat arrays so we can index them
    // Each entry in TESTD.BIN: [uint8 label][uint8 x 784]
    uint8_t *labels = kmalloc(n);
    // images: pointers into the mmapped file data
    uint8_t **imgs = kmalloc(n * sizeof(uint8_t*));
    uint8_t *p = base + 4;
    for (int i = 0; i < n; i++) {
        labels[i] = *p++;
        imgs[i]   = p;
        p += 784;
    }
    printk("Labels:");
    for (int i = 0; i < n; i++) printk(" %c", asl_labels[labels[i]]);
    printk("\n");

    // Gradient norms at epoch 0 (before any training)
    float init_loss = 0;
    for (int i = 0; i < n; i++)
        init_loss += asl_train_forward(&m, imgs[i], labels[i]);
    init_loss /= (float)n;

    // One backward on sample 0 to show grad norms
    asl_zero_grad(&m);
    asl_train_forward(&m, imgs[0], labels[0]);
    asl_backward(&m, labels[0]);

    static const int   gsz[10] = {72,8,1152,16,4608,32,18432,64,1536,24};
    static const char *gnm[10] = {
        "conv1_w","conv1_b","conv2_w","conv2_b","conv3_w","conv3_b",
        "fc1_w",  "fc1_b",  "fc2_w",  "fc2_b"
    };
    float *gp[10] = {
        m.gconv1_w,m.gconv1_b,m.gconv2_w,m.gconv2_b,
        m.gconv3_w,m.gconv3_b,m.gfc1_w,  m.gfc1_b,
        m.gfc2_w,  m.gfc2_b
    };
    printk("Grad norms (sample 0):\n");
    for (int i = 0; i < 10; i++) {
        float s=0;
        for(int j=0;j<gsz[i];j++) s+=gp[i][j]*gp[i][j];
        printk("NORM %s ", gnm[i]); pf(_sqrtf(s)); printk("\n");
    }

    printk("init avg loss="); pf(init_loss); printk("\n");
    printk("--- training %d epochs ---\n", EPOCHS);

    // Training loop: each epoch = one pass over all n samples
    for (int epoch = 0; epoch < EPOCHS; epoch++) {
        // SGD pass over all samples
        for (int i = 0; i < n; i++) {
            asl_zero_grad(&m);
            asl_train_forward(&m, imgs[i], labels[i]);
            asl_backward(&m, labels[i]);
            asl_sgd_step(&m, LR);
        }

        // After epoch 0: print grad norms to confirm conv layers are updating
        if (epoch == 0) {
            // Accumulate grads over all samples (don't step, just measure)
            asl_zero_grad(&m);
            for (int i = 0; i < n; i++) {
                asl_train_forward(&m, imgs[i], labels[i]);
                asl_backward(&m, labels[i]);
            }
            printk("Grad norms after epoch 0 (all %d samples):\n", n);
            for (int i = 0; i < 10; i++) {
                float s = 0;
                for (int j = 0; j < gsz[i]; j++) s += gp[i][j]*gp[i][j];
                printk("NORM2 %s ", gnm[i]); pf(_sqrtf(s)); printk("\n");
            }
        }

        // Evaluate: loss over all n samples
        float total_loss = 0;
        for (int i = 0; i < n; i++)
            total_loss += asl_train_forward(&m, imgs[i], labels[i]);
        float avg_loss = total_loss / (float)n;

        printk("EPOCH %d loss=", epoch);
        pf(avg_loss);
        printk("\n");
    }

    // Final eval
    float final_loss = 0; int final_correct = 0;
    for (int i = 0; i < n; i++) {
        final_loss += asl_train_forward(&m, imgs[i], labels[i]);
        if (asl_train_argmax() == labels[i]) final_correct++;
    }
    final_loss /= (float)n;

    printk("--- done ---\n");
    printk("Final: loss="); pf(final_loss);
    printk(" acc=%d/%d\n", final_correct, n);
    if (final_correct == n)
        printk("PASS: 100%% accuracy on %d training samples\n", n);
    else
        printk("PARTIAL: %d/%d correct\n", final_correct, n);
}
