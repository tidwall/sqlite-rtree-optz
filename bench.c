#include <math.h>
#include <stdbool.h>
#include <unistd.h>
#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <stdint.h>
#include "sqlite3.h"

#ifdef __clang__
#pragma GCC diagnostic ignored "-Wcompound-token-split-by-macro"
#endif

#ifdef __GNUC__
#pragma GCC diagnostic ignored "-Wpedantic"
#endif

char *commaize(unsigned int n) {
    char s1[64];
    char *s2 = malloc(64);
    assert(s2);
    memset(s2, 0, sizeof(64));
    snprintf(s1, sizeof(s1), "%d", n);
    int i = strlen(s1)-1; 
    int j = 0;
	while (i >= 0) {
		if (j%3 == 0 && j != 0) {
            memmove(s2+1, s2, strlen(s2)+1);
            s2[0] = ',';
		}
        memmove(s2+1, s2, strlen(s2)+1);
		s2[0] = s1[i];
        i--;
        j++;
	}
	return s2;
}


#define bench(name, N, code) { \
    if (strlen(name) > 0) { \
        printf("%-14s ", name); \
    } \
    clock_t begin = clock(); \
    for (int i = 0; i < N; i++) { \
        (code); \
    } \
    clock_t end = clock(); \
    double elapsed_secs = (double)(end - begin) / CLOCKS_PER_SEC; \
    double ns_op = elapsed_secs/(double)N*1e9; \
    char *pops = commaize(N); \
    char *psec = commaize((double)N/elapsed_secs); \
    printf("%10s ops in %.3f secs %8.1f ns/op %11s op/sec", \
        pops, elapsed_secs, ns_op, psec); \
    free(psec); \
    free(pops); \
    printf("\n"); \
}

double rand_double() {
    return (double)rand() / ((double)RAND_MAX+1);
}

double *make_random_points(int N) {


    double *points = (double *)malloc(N*2*sizeof(double));
    assert(points);
    for (int i = 0; i < N; i++) {
        points[i*2+0] = rand_double() * 360.0 - 180.0;;
        points[i*2+1] = rand_double() * 180.0 - 90.0;;
    }
    return points;
}


unsigned int mkseed() {
    unsigned int seed = 0;
    FILE *f = fopen("/dev/random", "rb");
    assert(f);
    assert(fread(&seed, sizeof(unsigned int), 1, f) == 1);
    fclose(f);
    return seed;
}


// Fast 2D hilbert curve
// https://github.com/rawrunprotected/hilbert_curves
// Public Domain
static uint32_t hilbert_xy_to_index(uint32_t x, uint32_t y) {
    uint32_t A, B, C, D;

    // Initial prefix scan round, prime with x and y
    {
        uint32_t a = x ^ y;
        uint32_t b = 0xFFFF ^ a;
        uint32_t c = 0xFFFF ^ (x | y);
        uint32_t d = x & (y ^ 0xFFFF);

        A = a | (b >> 1);
        B = (a >> 1) ^ a;

        C = ((c >> 1) ^ (b & (d >> 1))) ^ c;
        D = ((a & (c >> 1)) ^ (d >> 1)) ^ d;
    }

    {
        uint32_t a = A;
        uint32_t b = B;
        uint32_t c = C;
        uint32_t d = D;

        A = ((a & (a >> 2)) ^ (b & (b >> 2)));
        B = ((a & (b >> 2)) ^ (b & ((a ^ b) >> 2)));

        C ^= ((a & (c >> 2)) ^ (b & (d >> 2)));
        D ^= ((b & (c >> 2)) ^ ((a ^ b) & (d >> 2)));
    }

    {
        uint32_t a = A;
        uint32_t b = B;
        uint32_t c = C;
        uint32_t d = D;

        A = ((a & (a >> 4)) ^ (b & (b >> 4)));
        B = ((a & (b >> 4)) ^ (b & ((a ^ b) >> 4)));

        C ^= ((a & (c >> 4)) ^ (b & (d >> 4)));
        D ^= ((b & (c >> 4)) ^ ((a ^ b) & (d >> 4)));
    }

    // Final round and projection
    {
        uint32_t a = A;
        uint32_t b = B;
        uint32_t c = C;
        uint32_t d = D;

        C ^= ((a & (c >> 8)) ^ (b & (d >> 8)));
        D ^= ((b & (c >> 8)) ^ ((a ^ b) & (d >> 8)));
    }

    // Undo transformation prefix scan
    uint32_t a = C ^ (C >> 1);
    uint32_t b = D ^ (D >> 1);

    // Recover index bits
    uint32_t i0 = x ^ y;
    uint32_t i1 = b | (0xFFFF ^ (i0 | a));

    // interleave(i0)
    i0 = (i0 | (i0 << 8)) & 0x00FF00FF;
    i0 = (i0 | (i0 << 4)) & 0x0F0F0F0F;
    i0 = (i0 | (i0 << 2)) & 0x33333333;
    i0 = (i0 | (i0 << 1)) & 0x55555555;

    // interleave(i1)
    i1 = (i1 | (i1 << 8)) & 0x00FF00FF;
    i1 = (i1 | (i1 << 4)) & 0x0F0F0F0F;
    i1 = (i1 | (i1 << 2)) & 0x33333333;
    i1 = (i1 | (i1 << 1)) & 0x55555555;

    return (i1 << 1) | i0;
}

uint32_t rtree_hilbert_xy(double x, double y, double xmin, double ymin,
    double xmax, double ymax)
{
    uint32_t ix = ((x - xmin) / (xmax - xmin)) * 0xFFFF;
    uint32_t iy = ((y - ymin) / (ymax - ymin)) * 0xFFFF;
    return hilbert_xy_to_index(ix, iy);
}


int point_compare(const void *a, const void *b) {
    const double *p1 = a;
    const double *p2 = b;
    uint32_t h1 = rtree_hilbert_xy(p1[0],p1[1], -180, -90, 180, 90);
    uint32_t h2 = rtree_hilbert_xy(p2[0],p2[1], -180, -90, 180, 90);
    return h1 < h2 ? -1 : h1 > h2;
}

void sort_points(double *points, int N) {
    qsort(points, N, sizeof(double)*2, point_compare);
} 


void test_bench(bool hilbert, int N, int twindow) {
    double *points = make_random_points(N);

    if (hilbert) {
        printf("-- HILBERT ORDER --\n");
    } else {
        printf("-- RANDOM ORDER --\n");
    }

    unlink("rtree.db");
    sqlite3 *db;
    assert(sqlite3_open_v2("rtree.db", &db, 
        SQLITE_OPEN_READWRITE|SQLITE_OPEN_CREATE, 0) == SQLITE_OK);
    assert(sqlite3_exec(db, 
        "CREATE VIRTUAL TABLE IF NOT EXISTS rects "
        "USING rtree(id, xmin, xmax, ymin, ymax)",
        0, 0, 0) == SQLITE_OK);
    sqlite3_stmt *stmt;
    assert(sqlite3_prepare_v2(db, 
        "INSERT INTO rects VALUES(?, ?, ?, ?, ?)", 
        -1, &stmt, 0) == SQLITE_OK);
    assert(sqlite3_exec(db, "BEGIN TRANSACTION", 0, 0, 0) == SQLITE_OK);
    int n = 0;
    bench("insert", N, {
        if (i == 0 && hilbert) {
            sort_points(points, N);
        }
        if (n == twindow) {
            assert(sqlite3_exec(db, "END TRANSACTION", 0, 0, 0) == SQLITE_OK);
            assert(sqlite3_exec(db, "BEGIN TRANSACTION", 0, 0, 0) == SQLITE_OK);
            n = 0;
        }
        double *point = &points[i*2];
        sqlite3_bind_int(stmt, 1, i);
        sqlite3_bind_double(stmt, 2, point[0]);
        sqlite3_bind_double(stmt, 3, point[0]);
        sqlite3_bind_double(stmt, 4, point[1]);
        sqlite3_bind_double(stmt, 5, point[1]);
        assert(sqlite3_step(stmt) == SQLITE_DONE);
        sqlite3_clear_bindings(stmt);
        sqlite3_reset(stmt);
        n++;
    });
    assert(sqlite3_exec(db, "END TRANSACTION", 0, 0, 0) == SQLITE_OK);
    assert(sqlite3_finalize(stmt) == SQLITE_OK);

if (0) {
    assert(sqlite3_prepare_v2(db, 
        "SELECT id FROM rects "
        "WHERE xmin >= ? AND xmax <= ? AND "
                "ymin >= ? AND ymax <= ?",
        -1, &stmt, 0) == SQLITE_OK);
    assert(sqlite3_exec(db, "BEGIN TRANSACTION", 0, 0, 0) == SQLITE_OK);
    n = 0;
    bench("search-item", N, {
        if (n == twindow) {
            assert(sqlite3_exec(db, "END TRANSACTION", 0, 0, 0) == SQLITE_OK);
            assert(sqlite3_exec(db, "BEGIN TRANSACTION", 0, 0, 0) == SQLITE_OK);
            n = 0;
        }
        double *point = &points[i*2];
        // printf("%f %f\n", point[0], point[1]);
        sqlite3_bind_double(stmt, 1, point[0]);
        sqlite3_bind_double(stmt, 2, point[0]);
        sqlite3_bind_double(stmt, 3, point[1]);
        sqlite3_bind_double(stmt, 4, point[1]);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            int id = sqlite3_column_int(stmt, 0);
            assert(id == i);
        }
        sqlite3_clear_bindings(stmt);
        sqlite3_reset(stmt);
        n++;
    });
    assert(sqlite3_exec(db, "END TRANSACTION", 0, 0, 0) == SQLITE_OK);
    assert(sqlite3_finalize(stmt) == SQLITE_OK);
}

    assert(sqlite3_prepare_v2(db, 
        "SELECT count(id) FROM rects "
        "WHERE xmin >= ? AND xmax <= ? AND "
                "ymin >= ? AND ymax <= ?",
        -1, &stmt, 0) == SQLITE_OK);

    // search-1%
    bench("search-1%", 1000, {
        const double p = 0.01;
        double min[2];
        double max[2];
        min[0] = rand_double() * 360.0 - 180.0;
        min[1] = rand_double() * 180.0 - 90.0;
        max[0] = min[0] + 360.0*p;
        max[1] = min[1] + 180.0*p;
        sqlite3_bind_double(stmt, 1, min[0]);
        sqlite3_bind_double(stmt, 2, max[0]);
        sqlite3_bind_double(stmt, 3, min[1]);
        sqlite3_bind_double(stmt, 4, max[1]);
        assert(sqlite3_step(stmt) == SQLITE_ROW);
        sqlite3_clear_bindings(stmt);
        sqlite3_reset(stmt);
        n++;
    });

    bench("search-5%", 1000, {
        const double p = 0.05;
        double min[2];
        double max[2];
        min[0] = rand_double() * 360.0 - 180.0;
        min[1] = rand_double() * 180.0 - 90.0;
        max[0] = min[0] + 360.0*p;
        max[1] = min[1] + 180.0*p;
        sqlite3_bind_double(stmt, 1, min[0]);
        sqlite3_bind_double(stmt, 2, max[0]);
        sqlite3_bind_double(stmt, 3, min[1]);
        sqlite3_bind_double(stmt, 4, max[1]);
        assert(sqlite3_step(stmt) == SQLITE_ROW);
        sqlite3_clear_bindings(stmt);
        sqlite3_reset(stmt);
        n++;
    });

    bench("search-10%", 1000, {
        const double p = 0.10;
        double min[2];
        double max[2];
        min[0] = rand_double() * 360.0 - 180.0;
        min[1] = rand_double() * 180.0 - 90.0;
        max[0] = min[0] + 360.0*p;
        max[1] = min[1] + 180.0*p;
        sqlite3_bind_double(stmt, 1, min[0]);
        sqlite3_bind_double(stmt, 2, max[0]);
        sqlite3_bind_double(stmt, 3, min[1]);
        sqlite3_bind_double(stmt, 4, max[1]);
        assert(sqlite3_step(stmt) == SQLITE_ROW);
        sqlite3_clear_bindings(stmt);
        sqlite3_reset(stmt);
        n++;
    });


    assert(sqlite3_finalize(stmt) == SQLITE_OK);
    assert(sqlite3_close_v2(db) == SQLITE_OK);

    free(points);
}

int main() {
    unsigned int seed = getenv("SEED")?atoll(getenv("SEED")):mkseed();
    int N = getenv("N")?atoi(getenv("N")):1000000;
    int twindow = getenv("TWINDOW")?atoi(getenv("TWINDOW")):100000;
    printf("SEED=%u N=%d TWINDOW=%d\n", seed, N, twindow);
    srand(seed);

    test_bench(false, N, twindow);
    test_bench(true, N, twindow);
    return 0;
}