#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <sys/time.h>
#include <unistd.h>

typedef struct { int d1, d2; long count; } Entry;

// Somma di riga r di J
static long row_sum(long **J, int n, int r) {
    long s = 0;
    for (int c = 0; c < n; c++) s += J[r][c];
    return s;
}

int main(int argc, char *argv[]) {
    if (argc != 4) {
        fprintf(stderr, "Usage: %s <input.nkk> <num_steps> <output.nkk>\n",
                argv[0]);
        return EXIT_FAILURE;
    }
    const char *infile  = argv[1];
    long num_steps       = atol(argv[2]);
    const char *outfile  = argv[3];

    // 1) Leggi tutte le entry da infile, in array per mantenere ordine
    Entry *entries = NULL;
    size_t cap = 0, nents = 0;
    int maxd = 0;
    {
        FILE *fin = fopen(infile, "r");
        if (!fin) { perror("open input"); return EXIT_FAILURE; }
        char line[256];
        while (fgets(line, sizeof(line), fin)) {
            int d1, d2;
            long cnt;
            if (sscanf(line, "%d,%d,%ld", &d1, &d2, &cnt) != 3) 
                continue;
            if ((size_t)nents == cap) {
                cap = cap ? cap * 2 : 16;
                entries = realloc(entries, cap * sizeof *entries);
                if (!entries) { perror("realloc"); return EXIT_FAILURE; }
            }
            entries[nents++] = (Entry){d1, d2, cnt};
            if (d1 > maxd) maxd = d1;
            if (d2 > maxd) maxd = d2;
        }
        fclose(fin);
    }

    int n = maxd + 1;
    // 2) Costruisci matrice simmetrica J[n][n]
    long **J = malloc(n * sizeof *J);
    if (!J) { perror("malloc"); return EXIT_FAILURE; }
    for (int i = 0; i < n; i++) {
        J[i] = calloc(n, sizeof *J[i]);
        if (!J[i]) { perror("calloc"); return EXIT_FAILURE; }
    }
    // Popola da entries[]
    for (size_t i = 0; i < nents; i++) {
        int d1 = entries[i].d1, d2 = entries[i].d2;
        J[d1][d2] = entries[i].count;
        J[d2][d1] = entries[i].count;
    }

    // 3) Semina RNG con microsecondi ^ PID
    struct timeval tv;
    gettimeofday(&tv, NULL);
    unsigned seed = (unsigned)(tv.tv_sec ^ (tv.tv_usec << 10) ^ getpid());
    srand(seed);

    // 4) Mutazioni: 2-edge-swap “disjoint” con controllo capacità
    for (long step = 0; step < num_steps; step++) {
        int i1, j1, i2, j2;
        long x1, x2, max_k, k;

        do {
            // (a) pescaggio di due archi (i<j) con J[i][j]>=2 e disjoint
            do {
                i1 = rand() % (n - 1);
                j1 = i1 + 1 + rand() % (n - i1 - 1);
                x1 = J[i1][j1];
            } while (x1 < 2);

            do {
                i2 = rand() % (n - 1);
                j2 = i2 + 1 + rand() % (n - i2 - 1);
                x2 = J[i2][j2];
            } while (
                x2 < 2
                || (i2==i1 && j2==j1)
                || i2==i1 || i2==j1 || j2==i1 || j2==j1
            );

            // (b) calcola n_k per ogni grado coinvolto
            long nk_i1 = row_sum(J,n,i1) / i1;
            long nk_j1 = row_sum(J,n,j1) / j1;
            long nk_i2 = row_sum(J,n,i2) / i2;
            long nk_j2 = row_sum(J,n,j2) / j2;

            // (c) capacità residue
            long cap12    = (i1 != j2 ? nk_i1*nk_j2   : nk_i1*(nk_i1-1));
            long cap21    = (i2 != j1 ? nk_i2*nk_j1   : nk_i2*(nk_i2-1));
            long avail12  = cap12 - J[i1][j2];
            long avail21  = cap21 - J[i2][j1];

            // (d) determina max_k
            max_k = x1 < x2 ? x1 : x2;
            if (avail12 < max_k) max_k = avail12;
            if (avail21 < max_k) max_k = avail21;

        } while (max_k < 1);

        // (e) esegui lo swap
        k = 1 + rand() % max_k;
        J[i1][j1] -= k;  J[j1][i1] -= k;
        J[i2][j2] -= k;  J[j2][i2] -= k;
        J[i1][j2] += k;  J[j2][i1] += k;
        J[i2][j1] += k;  J[j1][i2] += k;
    }

    // 5) Scrivi output **nello stesso ordine e stile** di input
    FILE *fout = fopen(outfile, "w");
    if (!fout) { perror("open output"); return EXIT_FAILURE; }
    for (size_t i = 0; i < nents; i++) {
        int d1 = entries[i].d1, d2 = entries[i].d2;
        long cnt = J[d1][d2];
        fprintf(fout, "%d,%d,%ld\n", d1, d2, cnt);
    }
    fclose(fout);

    // 6) Pulizia
    for (int i = 0; i < n; i++) free(J[i]);
    free(J);
    free(entries);

    return EXIT_SUCCESS;
}
