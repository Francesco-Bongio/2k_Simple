#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <igraph.h>
#include <glib.h>

// Definizione di tipi per le strutture dati
typedef GHashTable mapii;       // chiave: (int), valore: (int)
typedef GHashTable mapi_mapii;  // chiave: (int), valore: (mapii*)

#ifndef GINT_TO_POINTER
#define GINT_TO_POINTER(i)  ((gpointer)(glong)(i))
#endif
#ifndef GPOINTER_TO_INT
#define GPOINTER_TO_INT(p)  ((gint)(glong)(p))
#endif

/* ------------------------------------------------------------------------
   compute_jdm_from_igraph(g)
   - Per ogni nodo calcola il grado d.
   - Per ogni arco (u,v), incrementa nkk[d(u)][d(v)] e nkk[d(v)][d(u)].
   - Restituisce un nuovo GHashTable (mapi_mapii).
   ------------------------------------------------------------------------ */
mapi_mapii* compute_jdm_from_igraph(const igraph_t *g) {
    igraph_integer_t n = igraph_vcount(g);

    // Vettore per memorizzare il grado di ogni nodo
    igraph_vector_int_t deg;
    igraph_vector_int_init(&deg, n);
    igraph_degree(g, &deg, igraph_vss_all(), IGRAPH_ALL, IGRAPH_NO_LOOPS);

    // Crea la struttura JDM: GHashTable<k, GHashTable<l, conteggio>>
    mapi_mapii *result = g_hash_table_new(g_direct_hash, g_direct_equal);

    // Itera su tutti gli archi per riempire la JDM
    igraph_eit_t eit;
    igraph_eit_create(g, igraph_ess_all(IGRAPH_EDGEORDER_ID), &eit);
    while (!IGRAPH_EIT_END(eit)) {
        igraph_integer_t eid = IGRAPH_EIT_GET(eit);
        igraph_integer_t u, v;
        igraph_edge(g, eid, &u, &v);

        int deg_u = (int) igraph_vector_int_get(&deg, u);
        int deg_v = (int) igraph_vector_int_get(&deg, v);

        // Aggiorna nkk[deg_u][deg_v]
        mapii *mapU = g_hash_table_lookup(result, GINT_TO_POINTER(deg_u));
        if (!mapU) {
            mapU = g_hash_table_new(g_direct_hash, g_direct_equal);
            g_hash_table_insert(result, GINT_TO_POINTER(deg_u), mapU);
        }
        int oldValUV = GPOINTER_TO_INT(g_hash_table_lookup(mapU, GINT_TO_POINTER(deg_v)));
        g_hash_table_insert(mapU, GINT_TO_POINTER(deg_v), GINT_TO_POINTER(oldValUV + 1));

        // Aggiorna nkk[deg_v][deg_u]
        mapii *mapV = g_hash_table_lookup(result, GINT_TO_POINTER(deg_v));
        if (!mapV) {
            mapV = g_hash_table_new(g_direct_hash, g_direct_equal);
            g_hash_table_insert(result, GINT_TO_POINTER(deg_v), mapV);
        }
        int oldValVU = GPOINTER_TO_INT(g_hash_table_lookup(mapV, GINT_TO_POINTER(deg_u)));
        g_hash_table_insert(mapV, GINT_TO_POINTER(deg_u), GINT_TO_POINTER(oldValVU + 1));

        IGRAPH_EIT_NEXT(eit);
    }
    igraph_eit_destroy(&eit);

    igraph_vector_int_destroy(&deg);
    return result;
}

/* ------------------------------------------------------------------------
   write_jdm(nkk)
   - Stampa tutte le coppie (k,l) in formato "k,l,valore".
   ------------------------------------------------------------------------ */
void write_jdm(mapi_mapii *nkk) {
    // Itera su k (chiavi esterne)
    GHashTableIter outer;
    gpointer key_k, val_k;
    g_hash_table_iter_init(&outer, nkk);
    while (g_hash_table_iter_next(&outer, &key_k, &val_k)) {
        int k = GPOINTER_TO_INT(key_k);
        mapii *mapKL = (mapii*) val_k;

        // Per ogni l interno, stampa "k,l,valore"
        GHashTableIter inner;
        gpointer key_l, val_l;
        g_hash_table_iter_init(&inner, mapKL);
        while (g_hash_table_iter_next(&inner, &key_l, &val_l)) {
            int l = GPOINTER_TO_INT(key_l);
            int val = GPOINTER_TO_INT(val_l);
            printf("%d,%d,%d\n", k, l, val);
        }
    }
}

/* ------------------------------------------------------------------------
   main(n, p):
   1. Crea un grafo random Erdős–Rényi G(n,p) (non diretto, senza loop).
   2. Calcola la JDM di questo grafo.
   3. Stampa la JDM in righe "k,l,valore".
   ------------------------------------------------------------------------ */
int main(int argc, char *argv[]) {
    if (argc < 3) {
        fprintf(stderr, "Uso: %s <n> <p>\n", argv[0]);
        return 1;
    }

    int n = atoi(argv[1]);
    double p = atof(argv[2]);

    // Seed per il generatore di numeri casuali di igraph e di C
    srand((unsigned)time(NULL));
    igraph_rng_seed(igraph_rng_default(), (unsigned long)time(NULL));

    // Costruisce il grafo G(n,p)
    igraph_t g;
    igraph_erdos_renyi_game(&g,
                            IGRAPH_ERDOS_RENYI_GNP,
                            n,      // numero di nodi
                            p,      // probabilità di edge
                            IGRAPH_UNDIRECTED,
                            IGRAPH_NO_LOOPS);

    // Calcola la JDM di questo grafo random
    mapi_mapii *nkk = compute_jdm_from_igraph(&g);

    // Stampa la JDM in formato "k,l,valore"
    write_jdm(nkk);

    // Pulizia
    igraph_destroy(&g);

    // Libera la struttura JDM
    GHashTableIter outer;
    gpointer key, val;
    g_hash_table_iter_init(&outer, nkk);
    while (g_hash_table_iter_next(&outer, &key, &val)) {
        mapii *inner_map = (mapii*) val;
        g_hash_table_destroy(inner_map);
    }
    g_hash_table_destroy(nkk);

    return 0;
}
