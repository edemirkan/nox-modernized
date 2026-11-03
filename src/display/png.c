/* A small PNG writer (RGB, Paeth filter, deflate with fixed Huffman codes and LZ77 matching) for screenshots. */
#include "display.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct { uint8_t *p; size_t n, cap; uint32_t bits; int nb; } out_t;
static void put(out_t *o, uint8_t b) {
    if (o->n == o->cap) { o->cap = o->cap ? o->cap * 2 : 1 << 16; o->p = realloc(o->p, o->cap); }
    o->p[o->n++] = b;
}
static void bits(out_t *o, uint32_t v, int n) { /* LSB first */
    o->bits |= v << o->nb; o->nb += n;
    while (o->nb >= 8) { put(o, (uint8_t)o->bits); o->bits >>= 8; o->nb -= 8; }
}
static void huff(out_t *o, uint32_t code, int n) { /* Huffman codes go MSB first */
    uint32_t r = 0;
    for (int i = 0; i < n; i++) r |= ((code >> i) & 1) << (n - 1 - i);
    bits(o, r, n);
}
static void lit(out_t *o, int v) {
    if (v < 144) huff(o, 0x30 + v, 8);
    else if (v < 256) huff(o, 0x190 + v - 144, 9);
    else if (v < 280) huff(o, v - 256, 7);
    else huff(o, 0xC0 + v - 280, 8);
}
static const uint16_t k_lbase[] = {3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258};
static const uint8_t k_lext[] = {0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0};
static const uint16_t k_dbase[] = {1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,1025,1537,2049,3073,
                                   4097,6145,8193,12289,16385,24577};
static const uint8_t k_dext[] = {0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13};

static void match(out_t *o, int len, int dist) {
    int i = 28;
    while (k_lbase[i] > len) i--;
    lit(o, 257 + i);
    if (k_lext[i]) bits(o, len - k_lbase[i], k_lext[i]);
    int d = 29;
    while (k_dbase[d] > dist) d--;
    huff(o, d, 5);
    if (k_dext[d]) bits(o, dist - k_dbase[d], k_dext[d]);
}

#define HBITS 15
static void deflate(out_t *o, const uint8_t *s, size_t n) {
    int *head = malloc(sizeof(int) << HBITS), *prev = malloc(sizeof(int) * 32768);
    for (int i = 0; i < 1 << HBITS; i++) head[i] = -1;
    put(o, 0x78); put(o, 0x01);
    bits(o, 1, 1); bits(o, 1, 2); /* one final block, fixed codes */
    size_t i = 0;
    while (i < n) {
        int best = 0, bd = 0;
        if (i + 3 <= n) {
            uint32_t h = ((s[i] << 10) ^ (s[i + 1] << 5) ^ s[i + 2]) & ((1 << HBITS) - 1);
            int c = head[h], depth = 32;
            size_t max = n - i < 258 ? n - i : 258;
            while (c >= 0 && i - c <= 32768 && depth--) {
                size_t l = 0;
                while (l < max && s[c + l] == s[i + l]) l++;
                if ((int)l > best) { best = (int)l; bd = (int)(i - c); if (l == max) break; }
                c = prev[c & 32767];
            }
            prev[i & 32767] = head[h]; head[h] = (int)i;
        }
        if (best >= 3) {
            match(o, best, bd);
            for (size_t k = i + 1; k < i + best && k + 3 <= n; k++) {
                uint32_t h = ((s[k] << 10) ^ (s[k + 1] << 5) ^ s[k + 2]) & ((1 << HBITS) - 1);
                prev[k & 32767] = head[h]; head[h] = (int)k;
            }
            i += best;
        } else lit(o, s[i++]);
    }
    lit(o, 256);
    if (o->nb) bits(o, 0, 8 - o->nb);
    uint32_t a = 1, b = 0;
    for (size_t k = 0; k < n; k++) { a = (a + s[k]) % 65521; b = (b + a) % 65521; }
    uint32_t ad = (b << 16) | a;
    put(o, ad >> 24); put(o, ad >> 16); put(o, ad >> 8); put(o, ad);
    free(head); free(prev);
}

static uint32_t crc_tab[256];
static uint32_t crc(uint32_t c, const uint8_t *p, size_t n) {
    if (!crc_tab[1])
        for (uint32_t i = 0; i < 256; i++) { uint32_t v = i; for (int k = 0; k < 8; k++) v = v & 1 ? 0xEDB88320u ^ (v >> 1) : v >> 1; crc_tab[i] = v; }
    for (size_t i = 0; i < n; i++) c = crc_tab[(c ^ p[i]) & 255] ^ (c >> 8);
    return c;
}
static void chunk(FILE *f, const char *type, const uint8_t *d, uint32_t n) {
    uint8_t len[4] = {n >> 24, n >> 16, n >> 8, n};
    fwrite(len, 1, 4, f); fwrite(type, 1, 4, f); if (n) fwrite(d, 1, n, f);
    uint32_t c = crc(crc(0xFFFFFFFFu, (const uint8_t *)type, 4), d, n) ^ 0xFFFFFFFFu;
    uint8_t cb[4] = {c >> 24, c >> 16, c >> 8, c};
    fwrite(cb, 1, 4, f);
}
static int paeth(int a, int b, int c) {
    int p = a + b - c, pa = abs(p - a), pb = abs(p - b), pc = abs(p - c);
    return pa <= pb && pa <= pc ? a : pb <= pc ? b : c;
}

/* px: 0xAARRGGBB, w*h */
int dsp_write_png(const char *path, const uint32_t *px, int w, int h) {
    size_t row = (size_t)w * 3 + 1;
    uint8_t *raw = malloc(row * h), *rgb = malloc((size_t)w * 3 * 2);
    uint8_t *cur = rgb, *up = rgb + w * 3;
    memset(up, 0, w * 3);
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            uint32_t v = px[y * w + x];
            cur[x * 3] = v >> 16; cur[x * 3 + 1] = v >> 8; cur[x * 3 + 2] = v;
        }
        uint8_t *r = raw + y * row;
        r[0] = 4; /* Paeth */
        for (int i = 0; i < w * 3; i++) {
            int a = i >= 3 ? cur[i - 3] : 0, b = up[i], c = i >= 3 ? up[i - 3] : 0;
            r[1 + i] = (uint8_t)(cur[i] - paeth(a, b, c));
        }
        uint8_t *t = cur; cur = up; up = t;
    }
    out_t o = {0};
    deflate(&o, raw, row * h);
    free(raw); free(rgb);
    FILE *f = fopen(path, "wb");
    if (!f) { free(o.p); return 0; }
    static const uint8_t sig[8] = {0x89, 'P', 'N', 'G', 13, 10, 26, 10};
    fwrite(sig, 1, 8, f);
    uint8_t ih[13] = {w >> 24, w >> 16, w >> 8, w, h >> 24, h >> 16, h >> 8, h, 8, 2, 0, 0, 0};
    chunk(f, "IHDR", ih, 13);
    chunk(f, "IDAT", o.p, (uint32_t)o.n);
    chunk(f, "IEND", NULL, 0);
    fclose(f);
    free(o.p);
    return 1;
}
