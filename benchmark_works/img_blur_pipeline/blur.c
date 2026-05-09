/*
 * blur.c  --  3x3 box blur on an embedded grayscale image.
 *
 * Header produced by convert_imagetoheader.py -- it declares:
 *     #define image_width   W
 *     #define image_height  H
 *     #define image_size    (W * H)
 *     static const unsigned char image_data[image_size] = { ... };
 *
 * We rely on those #defines for the dimensions so you can swap in a
 * different image by just regenerating the header; no recompile-time
 * flags needed.
 *
 * Why this shape works for a gem5 cache-compression study:
 *   - image_data lives in .rodata  -> reads flow through L1D/L2/L3
 *     from a fixed, reproducible virtual address.
 *   - out_buf   lives in BSS       -> writes also flow through the
 *     full cache hierarchy, so the LLC trace captures both halves of
 *     the workload's memory behavior.
 *   - No file I/O, no argv.  gem5 SE mode is happier, and the trace
 *     is deterministic run to run.
 *
 * Build (for gem5 SE, x86-64 static):
 *     gcc -O2 -static -o blur blur.c
 *
 * Run under gem5:
 *     gem5.opt --debug-flags=Cache --debug-file=llc_raw.log \
 *              gem5_se_config.py --cmd=./blur
 */

#include <stdio.h>
#include <stdint.h>

#include "image_lenna_data.h"   /* produced by convert_imagetoheader.py */

/* We use the header's own macros directly (image_width / image_height /
 * image_size) rather than aliasing to IMG_W / IMG_H -- any short alias
 * risks colliding with a future include-guard name. */

/* Output buffer: static -> lives in BSS so writes traverse L1/L2/L3
 * exactly like a heap buffer would.  Size is a compile-time constant
 * from the header, so no VLA / no malloc. */
static unsigned char out_buf[image_size];

/* Clamp coord to [0, max-1] so the 3x3 window never reads OOB at the
 * image border. */
static int clamp_int(int v, int max) {
    if (v < 0) return 0;
    if (v >= max) return max - 1;
    return v;
}

int main(void) {
    /* 3x3 box blur.  Sum 9 neighbors, divide by 9.
     * Max sum = 9 * 255 = 2295, comfortably in int range. */
    for (int y = 0; y < image_height; y++) {
        for (int x = 0; x < image_width; x++) {
            int sum = 0;
            for (int dy = -1; dy <= 1; dy++) {
                int yy = clamp_int(y + dy, image_height);
                for (int dx = -1; dx <= 1; dx++) {
                    int xx = clamp_int(x + dx, image_width);
                    sum += image_data[yy * image_width + xx];
                }
            }
            out_buf[y * image_width + x] = (unsigned char)(sum / 9);
        }
    }

    /* Touch the output buffer's endpoints so the optimizer can't
     * dead-strip the whole blur loop, and so gem5 stdout confirms
     * the workload actually ran. */
    printf("blur %dx%d done. out[0]=%u out[last]=%u\n",
           image_width, image_height,
           (unsigned)out_buf[0],
           (unsigned)out_buf[image_size - 1]);

    return 0;
}
