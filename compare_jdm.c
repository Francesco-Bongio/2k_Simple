#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <glib.h>
#include <igraph.h>

typedef GHashTable mapii;       // chiave = (int), valore = (int)
typedef GHashTable mapi_mapii;  // chiave = (int), valore = (mapii *)

/* Macros per convertire tra int e gpointer */
#ifndef GINT_TO_POINTER
#define GINT_TO_POINTER(i)  ((gpointer)(glong)(i))
#endif
#ifndef GPOINTER_TO_INT
#define GPOINTER_TO_INT(p)  ((gint)(glong)(p))
#endif

/* --------------------------------------------------------------------
   load_nkk(filename, nkk)

   Legge un file .nkk riga per riga, con formato "k,l,value".
   - k e l sono gradi (interi)
   - value è quante coppie (k,l) devono comparire nel JDM

   Riempie la GHashTable 'nkk' così:
     nkk[k][l] = value
   -------------------------------------------------------------------- */
void load_nkk(const char *filename, mapi_mapii *nkk) {
    FILE *f = fopen(filename, "r");
    if (!f) {
        fprintf(stderr, "Impossibile aprire il file .nkk: %s\n", filename);
        exit(EXIT_FAILURE);
    }

    char line[1024];
    while (fgets(line, sizeof(line), f)) {
        char *trim_line = g_strstrip(line);
        if (strlen(trim_line) == 0) continue; // salta righe vuote

        int k, l, val;
        if (sscanf(trim_line, "%d,%d,%d", &k, &l, &val) == 3) {
            mapii *inner = g_hash_table_lookup(nkk, GINT_TO_POINTER(k));
            if (!inner) {
                inner = g_hash_table_new(g_direct_hash, g_direct_equal);
                g_hash_table_insert(nkk, GINT_TO_POINTER(k), inner);
            }
            g_hash_table_insert(inner, GINT_TO_POINTER(l), GINT_TO_POINTER(val));
        } else {
            fprintf(stderr, "Attenzione: riga non valida in .nkk: %s\n", trim_line);
        }
    }

    fclose(f);
}

/* --------------------------------------------------------------------
   build_igraph_from_edgelist(filename, g)

   Legge un file di edge list, dove ogni riga è "u,v".
   - Crea un grafo igraph con i vertici trovati
   - Aggiunge un edge per ogni riga letta
   (Gestisce o ignora eventuali loop)
   -------------------------------------------------------------------- */

/* Struttura per memorizzare un arco non diretto (forzando u <= v). */
typedef struct {
    int u;
    int v;
} UndirectedEdge;

/* Funzione hash per UndirectedEdge. Combina la hash di u e v. */
static guint edge_hash(gconstpointer key) {
    const UndirectedEdge *e = key;
    // Metodo semplice: shift di 16 bit di u e XOR con v
    return (guint) ( (e->u << 16) ^ (e->v & 0xFFFF) );
}

/* Funzione di eguaglianza per UndirectedEdge. Due edge sono uguali se (u==u && v==v). */
static gboolean edge_equal(gconstpointer a, gconstpointer b) {
    const UndirectedEdge *e1 = a;
    const UndirectedEdge *e2 = b;
    return (e1->u == e2->u) && (e1->v == e2->v);
}

void build_igraph_from_edgelist(const char *filename, igraph_t *g) {
    FILE *f = fopen(filename, "r");
    if (!f) {
        fprintf(stderr, "Impossibile aprire il file di edgelist: %s\n", filename);
        exit(EXIT_FAILURE);
    }

    int max_node_id = -1;

    /* 
       Usiamo una GHashTable<UndirectedEdge, GINT_TO_POINTER(1)> 
       per memorizzare gli archi unici (senza duplicati).
    */
    GHashTable *edge_set = g_hash_table_new_full(
        edge_hash,       
        edge_equal,      
        g_free,          
        NULL             
    );

    char line[256];
    while (fgets(line, sizeof(line), f)) {
        char *trim_line = g_strstrip(line);
        if (strlen(trim_line) == 0) {
            continue;  // salta righe vuote
        }

        int u, v;
        if (sscanf(trim_line, "%d,%d", &u, &v) == 2) {
            if (u > max_node_id) max_node_id = u;
            if (v > max_node_id) max_node_id = v;

            // Mettiamo u <= v
            if (v < u) {
                int temp = u;
                u = v;
                v = temp;
            }

            // Creiamo un nuovo UndirectedEdge
            UndirectedEdge *e = g_new(UndirectedEdge, 1);
            e->u = u;
            e->v = v;

            // Inseriamo nella GHashTable (la value può essere un qualunque puntatore non nullo)
            g_hash_table_replace(edge_set, e, GINT_TO_POINTER(1));
        }
    }
    fclose(f);

    // Ora sappiamo quanti nodi servono: max_node_id + 1
    igraph_empty(g, max_node_id + 1, /*directed=*/0);

    // Convertiamo gli archi unici in un igraph_vector_int_t per igraph_add_edges
    guint n_edges = g_hash_table_size(edge_set);
    igraph_vector_int_t edge_vector;
    igraph_vector_int_init(&edge_vector, 2 * n_edges);

    // Scriviamo i nodi degli archi in edge_vector
    GHashTableIter iter;
    gpointer key, value;
    int idx = 0;

    g_hash_table_iter_init(&iter, edge_set);
    while (g_hash_table_iter_next(&iter, &key, &value)) {
        UndirectedEdge *e = (UndirectedEdge *)key;
        igraph_vector_int_set(&edge_vector, 2*idx,   e->u);
        igraph_vector_int_set(&edge_vector, 2*idx+1, e->v);
        idx++;
    }

    // Aggiungiamo gli archi al grafo igraph
    igraph_add_edges(g, &edge_vector, 0);

    // Pulizia
    igraph_vector_int_destroy(&edge_vector);
    g_hash_table_destroy(edge_set);
}


/* --------------------------------------------------------------------
   compute_jdm_from_igraph(g) -> mapi_mapii*

   1. Per ogni nodo, calcola il grado d.
   2. Per ogni arco (u,v):
      - k = grado(u), l = grado(v)
      - Incrementa nkk[k][l] di 1 e anche nkk[l][k] di 1 (simmetrico)

   Ritorna la mappa JDM (mapi_mapii).
   -------------------------------------------------------------------- */
mapi_mapii* compute_jdm_from_igraph(const igraph_t *g) {
    long int n = igraph_vcount(g);

    // Vettore dei gradi
    igraph_vector_int_t deg;
    igraph_vector_int_init(&deg, n);

    // Calcola il grado di tutti i nodi
    igraph_degree(g, &deg, igraph_vss_all(), IGRAPH_ALL, IGRAPH_NO_LOOPS);

    // Crea la struttura per il JDM
    mapi_mapii *result = g_hash_table_new(g_direct_hash, g_direct_equal);

    // Visita ogni arco
    igraph_eit_t eit;
    igraph_eit_create(g, igraph_ess_all(IGRAPH_EDGEORDER_ID), &eit);
    while (!IGRAPH_EIT_END(eit)) {
        igraph_integer_t eid = IGRAPH_EIT_GET(eit);

        igraph_integer_t from, to;
        igraph_edge(g, eid, &from, &to);

        int k = (int) igraph_vector_int_get(&deg, from);
        int l = (int) igraph_vector_int_get(&deg, to);

        // nkk[k][l]++
        mapii *mapKL = g_hash_table_lookup(result, GINT_TO_POINTER(k));
        if (!mapKL) {
            mapKL = g_hash_table_new(g_direct_hash, g_direct_equal);
            g_hash_table_insert(result, GINT_TO_POINTER(k), mapKL);
        }
        int old_val_kl = GPOINTER_TO_INT(g_hash_table_lookup(mapKL, GINT_TO_POINTER(l)));
        g_hash_table_insert(mapKL, GINT_TO_POINTER(l), GINT_TO_POINTER(old_val_kl + 1));

        // nkk[l][k]++ (simmetria)
        mapii *mapLK = g_hash_table_lookup(result, GINT_TO_POINTER(l));
        if (!mapLK) {
            mapLK = g_hash_table_new(g_direct_hash, g_direct_equal);
            g_hash_table_insert(result, GINT_TO_POINTER(l), mapLK);
        }
        int old_val_lk = GPOINTER_TO_INT(g_hash_table_lookup(mapLK, GINT_TO_POINTER(k)));
        g_hash_table_insert(mapLK, GINT_TO_POINTER(k), GINT_TO_POINTER(old_val_lk + 1));

        IGRAPH_EIT_NEXT(eit);
    }
    igraph_eit_destroy(&eit);
    igraph_vector_int_destroy(&deg);

    return result;
}

/* --------------------------------------------------------------------
   compare_jdms(nkk_in, nkk_out)

   Confronta due strutture JDM (mapi_mapii).
   Ritorna 0 se sono uguali, altrimenti un numero diverso da 0.
   Stampa anche i dettagli delle differenze trovate.
   -------------------------------------------------------------------- */
int compare_jdms(mapi_mapii *nkk_in, mapi_mapii *nkk_out) {
    int differences = 0;

    // Confronta tutte le voci di nkk_in
    GHashTableIter outer1;
    gpointer kkey, kval;
    g_hash_table_iter_init(&outer1, nkk_in);
    while (g_hash_table_iter_next(&outer1, &kkey, &kval)) {
        int k = GPOINTER_TO_INT(kkey);
        mapii *map_in = (mapii *)kval;
        mapii *map_out = (mapii *)g_hash_table_lookup(nkk_out, GINT_TO_POINTER(k));
        if (!map_out) {
            differences++;
            printf("[Differenza] Nessun valore per k=%d in nkk_out\n", k);
            continue;
        }

        // Confronta i l di map_in
        GHashTableIter inner1;
        gpointer lkey1, lval1;
        g_hash_table_iter_init(&inner1, map_in);
        while (g_hash_table_iter_next(&inner1, &lkey1, &lval1)) {
            int l = GPOINTER_TO_INT(lkey1);
            int val_in = GPOINTER_TO_INT(lval1);

            int val_out = 0;
            gpointer tmp = g_hash_table_lookup(map_out, GINT_TO_POINTER(l));
            if (tmp) val_out = GPOINTER_TO_INT(tmp);

            if (val_in != val_out) {
                differences++;
                printf("[Differenza] nkk_in[%d][%d] = %d, nkk_out[%d][%d] = %d\n",
                       k, l, val_in, k, l, val_out);
            }
        }
    }

    // Controlla se nkk_out ha voci in più
    GHashTableIter outer2;
    g_hash_table_iter_init(&outer2, nkk_out);
    while (g_hash_table_iter_next(&outer2, &kkey, &kval)) {
        int k = GPOINTER_TO_INT(kkey);
        mapii *map_out = (mapii *)kval;
        mapii *map_in = (mapii *)g_hash_table_lookup(nkk_in, GINT_TO_POINTER(k));

        // Se k non c'era in nkk_in
        if (!map_in) {
            GHashTableIter inner2;
            gpointer lkey2, lval2;
            g_hash_table_iter_init(&inner2, map_out);
            while (g_hash_table_iter_next(&inner2, &lkey2, &lval2)) {
                differences++;
                printf("[Differenza] nkk_out[%d][%d] = %d ma k non è presente in nkk_in\n",
                       k, GPOINTER_TO_INT(lkey2), GPOINTER_TO_INT(lval2));
            }
            continue;
        }

        // Se k c'è, controlla i l in più
        GHashTableIter inner2;
        gpointer lkey2, lval2;
        g_hash_table_iter_init(&inner2, map_out);
        while (g_hash_table_iter_next(&inner2, &lkey2, &lval2)) {
            int l = GPOINTER_TO_INT(lkey2);
            int val_out = GPOINTER_TO_INT(lval2);

            gpointer tmp = g_hash_table_lookup(map_in, GINT_TO_POINTER(l));
            if (!tmp) {
                differences++;
                printf("[Differenza] nkk_out[%d][%d] = %d ma l non è presente in nkk_in\n", 
                       k, l, val_out);
            }
        }
    }

    return differences;
}

int main(int argc, char *argv[]) {
    if (argc < 3) {
        fprintf(stderr, "Uso: %s input.nkk generated.graph\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    const char *nkk_file = argv[1];
    const char *graph_file = argv[2];

    /* 1) Carica il JDM di input (nkk_in) */
    mapi_mapii *nkk_in = g_hash_table_new(g_direct_hash, g_direct_equal);
    load_nkk(nkk_file, nkk_in);
    printf("Caricato JDM di input da '%s'\n", nkk_file);

    /* 2) Crea il grafo igraph dall'edgelist generata */
    igraph_t g;
    build_igraph_from_edgelist(graph_file, &g);
    printf("Caricato grafo da '%s'\n", graph_file);
    printf("Il grafo ha %ld nodi e %ld archi.\n", 
           (long)igraph_vcount(&g), (long)igraph_ecount(&g));

    /* 3) Calcola il JDM dal grafo (nkk_out) */
    mapi_mapii *nkk_out = compute_jdm_from_igraph(&g);
    printf("JDM calcolata dal grafo caricato.\n");

    /* 4) Confronta i due JDM */
    int diff = compare_jdms(nkk_in, nkk_out);
    if (diff == 0) {
        printf("[OK] la JDM calcolata corrisponde a quello di input.\n");
    } else {
        printf("[ATTENZIONE] Trovate %d differenze tra la JDM di input e quella calcolata.\n", diff);
    }

    /* 5) Pulizia finale */
    igraph_destroy(&g);

    // Libera nkk_in
    {
        GHashTableIter iter;
        gpointer key, val;
        g_hash_table_iter_init(&iter, nkk_in);
        while (g_hash_table_iter_next(&iter, &key, &val)) {
            mapii *inner = (mapii *)val;
            g_hash_table_destroy(inner);
        }
        g_hash_table_destroy(nkk_in);
    }

    // Libera nkk_out
    {
        GHashTableIter iter;
        gpointer key, val;
        g_hash_table_iter_init(&iter, nkk_out);
        while (g_hash_table_iter_next(&iter, &key, &val)) {
            mapii *inner = (mapii *)val;
            g_hash_table_destroy(inner);
        }
        g_hash_table_destroy(nkk_out);
    }

    return 0;
}
