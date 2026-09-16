/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * ECC200 方形 Data Matrix：ASCII 编码、Reed-Solomon 与 utah 放置。
 *
 * ECC200 square Data Matrix: ASCII encode, Reed-Solomon, and utah placement.
 */

#include "datamatrix.h"

#include <string.h>

#define DM_MAX_CW 98
#define DM_PAD 129
#define GF_POLY 0x12D

typedef struct {
    uint8_t data;
    uint8_t ecc;
    uint8_t mw;
    uint8_t mh;
    uint8_t regions;
} dm_sym_t;

static const dm_sym_t k_sym[] = {
    { 3, 5, 8, 8, 1 },
    { 5, 7, 10, 10, 1 },
    { 8, 10, 12, 12, 1 },
    { 12, 12, 14, 14, 1 },
    { 18, 14, 16, 16, 1 },
    { 22, 18, 18, 18, 1 },
    { 30, 20, 20, 20, 1 },
    { 36, 24, 22, 22, 1 },
    { 44, 28, 24, 24, 1 },
    { 62, 36, 14, 14, 4 },
};

static uint8_t s_alog[255];
static uint8_t s_log[256];
static int s_gf_ok;

static void gf_init(void) {
    if (s_gf_ok) return;
    int p = 1;
    for (int i = 0; i < 255; i++) {
        s_alog[i] = (uint8_t)p;
        s_log[p] = (uint8_t)i;
        p <<= 1;
        if (p >= 256) p ^= GF_POLY;
    }
    s_gf_ok = 1;
}

static uint8_t gf_mul(uint8_t a, uint8_t b) {
    if (a == 0 || b == 0) return 0;
    return s_alog[(s_log[a] + s_log[b]) % 255];
}

static int h_regions(int regions) {
    return regions >= 4 ? 2 : 1;
}

static int v_regions(int regions) {
    return regions >= 4 ? 2 : 1;
}

static int ascii_encode(const char* text, uint8_t* out, int max) {
    int n = 0;
    int i = 0;
    while (text[i] != '\0' && n < max) {
        unsigned char a = (unsigned char)text[i];
        unsigned char b = (unsigned char)text[i + 1];
        if (a >= '0' && a <= '9' && b >= '0' && b <= '9') {
            out[n++] = (uint8_t)(130 + (a - '0') * 10 + (b - '0'));
            i += 2;
        } else {
            out[n++] = (uint8_t)(a + 1);
            i++;
        }
    }
    return text[i] == '\0' ? n : -1;
}

static void rs_gen(int nc, uint8_t* g) {
    memset(g, 0, (size_t)(nc + 1));
    g[0] = 1;
    for (int i = 0; i < nc; i++) {
        uint8_t r = s_alog[i + 1];
        uint8_t ng[64];
        ng[0] = gf_mul(g[0], r);
        for (int j = 1; j <= i; j++) {
            ng[j] = g[j - 1] ^ gf_mul(g[j], r);
        }
        ng[i + 1] = 1;
        memcpy(g, ng, (size_t)(i + 2));
    }
}

static void rs_block(const uint8_t* data, int nd, uint8_t* ecc, int nc) {
    uint8_t poly[64];
    rs_gen(nc, poly);
    memset(ecc, 0, (size_t)nc);
    for (int i = 0; i < nd; i++) {
        uint8_t m = ecc[nc - 1] ^ data[i];
        for (int k = nc - 1; k > 0; k--) {
            ecc[k] = (m != 0 && poly[k] != 0)
                ? (uint8_t)(ecc[k - 1] ^ gf_mul(m, poly[k]))
                : ecc[k - 1];
        }
        ecc[0] = (m != 0 && poly[0] != 0) ? gf_mul(m, poly[0]) : 0;
    }
    for (int i = 0; i < nc / 2; i++) {
        uint8_t t = ecc[i];
        ecc[i] = ecc[nc - 1 - i];
        ecc[nc - 1 - i] = t;
    }
}

static uint8_t pad253(int pos) {
    int t = DM_PAD + (149 * pos) % 253 + 1;
    return (uint8_t)(t <= 254 ? t : t - 254);
}

static void place_module(
    uint8_t* bits, int nr, int nc, int row, int col, uint8_t cw, int bit
) {
    if (row < 0) {
        row += nr;
        col += 4 - ((nr + 4) % 8);
    }
    if (col < 0) {
        col += nc;
        row += 4 - ((nc + 4) % 8);
    }
    if (row < 0 || col < 0 || row >= nr || col >= nc) return;
    bits[(size_t)row * (size_t)nc + (size_t)col] =
        (cw & (uint8_t)(1 << (8 - bit))) ? 1 : 0;
}

static void utah(
    uint8_t* bits, int nr, int nc, int row, int col, uint8_t cw
) {
    place_module(bits, nr, nc, row - 2, col - 2, cw, 1);
    place_module(bits, nr, nc, row - 2, col - 1, cw, 2);
    place_module(bits, nr, nc, row - 1, col - 2, cw, 3);
    place_module(bits, nr, nc, row - 1, col - 1, cw, 4);
    place_module(bits, nr, nc, row - 1, col, cw, 5);
    place_module(bits, nr, nc, row, col - 2, cw, 6);
    place_module(bits, nr, nc, row, col - 1, cw, 7);
    place_module(bits, nr, nc, row, col, cw, 8);
}

static int unset(const uint8_t* bits, int nr, int nc, int col, int row) {
    if (row < 0 || col < 0 || row >= nr || col >= nc) return 0;
    return bits[(size_t)row * (size_t)nc + (size_t)col] == 0xFF;
}

static void __attribute__((noinline))
place(uint8_t* bits, int nr, int nc, const uint8_t* cw, int n_cw) {
    memset(bits, 0xFF, (size_t)nr * (size_t)nc);
    int pos = 0;
    int row = 4;
    int col = 0;
    do {
        if (row == nr && col == 0 && pos < n_cw) {
            place_module(bits, nr, nc, nr - 1, 0, cw[pos], 1);
            place_module(bits, nr, nc, nr - 1, 1, cw[pos], 2);
            place_module(bits, nr, nc, nr - 1, 2, cw[pos], 3);
            place_module(bits, nr, nc, 0, nc - 2, cw[pos], 4);
            place_module(bits, nr, nc, 0, nc - 1, cw[pos], 5);
            place_module(bits, nr, nc, 1, nc - 1, cw[pos], 6);
            place_module(bits, nr, nc, 2, nc - 1, cw[pos], 7);
            place_module(bits, nr, nc, 3, nc - 1, cw[pos], 8);
            pos++;
        }
        if (row == nr - 2 && col == 0 && (nc % 4) != 0 && pos < n_cw) {
            place_module(bits, nr, nc, nr - 3, 0, cw[pos], 1);
            place_module(bits, nr, nc, nr - 2, 0, cw[pos], 2);
            place_module(bits, nr, nc, nr - 1, 0, cw[pos], 3);
            place_module(bits, nr, nc, 0, nc - 4, cw[pos], 4);
            place_module(bits, nr, nc, 0, nc - 3, cw[pos], 5);
            place_module(bits, nr, nc, 0, nc - 2, cw[pos], 6);
            place_module(bits, nr, nc, 0, nc - 1, cw[pos], 7);
            place_module(bits, nr, nc, 1, nc - 1, cw[pos], 8);
            pos++;
        }
        if (row == nr - 2 && col == 0 && (nc % 8) == 4 && pos < n_cw) {
            place_module(bits, nr, nc, nr - 3, 0, cw[pos], 1);
            place_module(bits, nr, nc, nr - 2, 0, cw[pos], 2);
            place_module(bits, nr, nc, nr - 1, 0, cw[pos], 3);
            place_module(bits, nr, nc, 0, nc - 2, cw[pos], 4);
            place_module(bits, nr, nc, 0, nc - 1, cw[pos], 5);
            place_module(bits, nr, nc, 1, nc - 1, cw[pos], 6);
            place_module(bits, nr, nc, 2, nc - 1, cw[pos], 7);
            place_module(bits, nr, nc, 3, nc - 1, cw[pos], 8);
            pos++;
        }
        if (row == nr + 4 && col == 2 && (nc % 8) == 0 && pos < n_cw) {
            place_module(bits, nr, nc, nr - 1, 0, cw[pos], 1);
            place_module(bits, nr, nc, nr - 1, nc - 1, cw[pos], 2);
            place_module(bits, nr, nc, 0, nc - 3, cw[pos], 3);
            place_module(bits, nr, nc, 0, nc - 2, cw[pos], 4);
            place_module(bits, nr, nc, 0, nc - 1, cw[pos], 5);
            place_module(bits, nr, nc, 1, nc - 3, cw[pos], 6);
            place_module(bits, nr, nc, 1, nc - 2, cw[pos], 7);
            place_module(bits, nr, nc, 1, nc - 1, cw[pos], 8);
            pos++;
        }
        do {
            if (row < nr && col >= 0 && unset(bits, nr, nc, col, row) && pos < n_cw) {
                utah(bits, nr, nc, row, col, cw[pos++]);
            }
            row -= 2;
            col += 2;
        } while (row >= 0 && col < nc);
        row++;
        col += 3;
        do {
            if (row >= 0 && col < nc && unset(bits, nr, nc, col, row) && pos < n_cw) {
                utah(bits, nr, nc, row, col, cw[pos++]);
            }
            row += 2;
            col -= 2;
        } while (row < nr && col >= 0);
        row += 3;
        col++;
    } while (row < nr || col < nc);

    if (unset(bits, nr, nc, nc - 1, nr - 1)) {
        bits[(nr - 1) * nc + (nc - 1)] = 1;
        bits[(nr - 2) * nc + (nc - 2)] = 1;
    }
}

int dm_encode(const char* text, uint8_t* modules, int* dim) {
    if (text == NULL || text[0] == '\0' || modules == NULL || dim == NULL) {
        return -1;
    }
    gf_init();

    uint8_t raw[64];
    int n = ascii_encode(text, raw, (int)sizeof(raw));
    if (n < 1) return -1;

    const dm_sym_t* sy = NULL;
    for (size_t i = 0; i < sizeof(k_sym) / sizeof(k_sym[0]); i++) {
        if (n <= k_sym[i].data) {
            sy = &k_sym[i];
            break;
        }
    }
    if (sy == NULL) return -1;

    uint8_t data[62];
    memcpy(data, raw, (size_t)n);
    if (n < sy->data) data[n++] = DM_PAD;
    while (n < sy->data) {
        data[n] = pad253(n + 1);
        n++;
    }

    uint8_t ecc[36];
    rs_block(data, sy->data, ecc, sy->ecc);

    uint8_t cw[DM_MAX_CW];
    memcpy(cw, data, sy->data);
    memcpy(cw + sy->data, ecc, sy->ecc);
    int n_cw = sy->data + sy->ecc;

    int hr = h_regions(sy->regions);
    int vr = v_regions(sy->regions);
    int map_w = hr * sy->mw;
    int map_h = vr * sy->mh;
    uint8_t map[28 * 28];
    place(map, map_h, map_w, cw, n_cw);

    int sw = map_w + hr * 2;
    int sh = map_h + vr * 2;
    if (sw > DM_MAX_DIM || sh > DM_MAX_DIM || sw != sh) return -1;
    memset(modules, 0, (size_t)DM_MAX_DIM * DM_MAX_DIM);

    int my = 0;
    for (int y = 0; y < map_h; y++) {
        if (y % sy->mh == 0) {
            for (int x = 0; x < sw; x++) {
                modules[my * sw + x] = (uint8_t)((x % 2) == 0);
            }
            my++;
        }
        int mx = 0;
        for (int x = 0; x < map_w; x++) {
            if (x % sy->mw == 0) {
                modules[my * sw + mx] = 1;
                mx++;
            }
            modules[my * sw + mx] = map[y * map_w + x] == 1 ? 1 : 0;
            mx++;
            if (x % sy->mw == sy->mw - 1) {
                modules[my * sw + mx] = (uint8_t)((y % 2) == 0);
                mx++;
            }
        }
        my++;
        if (y % sy->mh == sy->mh - 1) {
            for (int x = 0; x < sw; x++) modules[my * sw + x] = 1;
            my++;
        }
    }

    *dim = sw;
    return 0;
}
