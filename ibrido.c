#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <sys/time.h>
#include <string.h>
#include <math.h>
#include <glib.h>
#include <igraph/igraph.h>

#define NO_AVOID (-1)

/* ===============================
   Strutture Dati
   =============================== */

/* FastGraph: struttura per il grafo costruito velocemente utilizzando una matrice di adiacenza.
   - total_nodes: numero di nodi (0..total_nodes-1)
   - adj_matrix: matrice di adiacenza (array lineare di dimensione total_nodes*total_nodes)
         dove adj_matrix[u * total_nodes + v] = 1 significa che esiste l'arco (u,v)
         e 0 significa che non esiste.
   - node_residual: array di lunghezza total_nodes che tiene traccia degli stub liberi per ogni nodo
         (usato solo durante la costruzione).
*/
typedef struct {
    int total_nodes;
    char *adj_matrix;
    int *node_residual;
} FastGraph;

/* ===============================
   1) Funzioni Helper per FastGraph
   =============================== */

/* Inizializza un FastGraph con n nodi e senza archi.
   Alloca la matrice di adiacenza (inizializzata a 0).
   L'array node_residual verrà impostato esternamente.
*/
int fastgraph_init(FastGraph *g, int n) {
    g->total_nodes = n;
    g->adj_matrix = calloc(n * n, sizeof(char));
    if (!g->adj_matrix) {
        fprintf(stderr, "Errore: impossibile allocare la matrice di adiacenza per %d nodi.\n", n);
        return 1;
    }
    g->node_residual = NULL; /* verrà impostato dal chiamante */
    return 0;
}

/* Libera la memoria occupata da un FastGraph.
   (node_residual non viene liberato qui, in quanto gestito altrove)
*/
void fastgraph_destroy(FastGraph *g) {
    if (g->adj_matrix) free(g->adj_matrix);
    g->adj_matrix = NULL;
    g->total_nodes = 0;
}

/* Verifica in O(1) se esiste l'arco (u,v). */
static inline int fastgraph_has_edge(const FastGraph *g, int u, int v) {
    return g->adj_matrix[u * g->total_nodes + v];
}

/* Aggiunge l'arco (u,v) in O(1). */
static inline void fastgraph_add_edge(FastGraph *g, int u, int v) {
    g->adj_matrix[u * g->total_nodes + v] = 1;
    g->adj_matrix[v * g->total_nodes + u] = 1;
}

/* Rimuove l'arco (u,v) in O(1). */
static inline void fastgraph_remove_edge(FastGraph *g, int u, int v) {
    g->adj_matrix[u * g->total_nodes + v] = 0;
    g->adj_matrix[v * g->total_nodes + u] = 0;
}

/* Ottiene i vicini del nodo u (complessità O(n)).
   L'array restituito è allocato dinamicamente e va liberato dal chiamante.
   *n_neighbors verrà impostato con il numero di vicini trovati.
*/
int *fastgraph_neighbors(const FastGraph *g, int u, int *n_neighbors) {
    int n = g->total_nodes;
    int count = 0;
    for (int v = 0; v < n; v++) {
        if (fastgraph_has_edge(g, u, v) && v != u)
            count++;
    }
    int *neighbors = malloc(count * sizeof(int));
    if (!neighbors) {
        *n_neighbors = 0;
        return NULL;
    }
    int idx = 0;
    for (int v = 0; v < n; v++) {
        if (fastgraph_has_edge(g, u, v) && v != u)
            neighbors[idx++] = v;
    }
    *n_neighbors = count;
    return neighbors;
}

/* ===============================
   2) Verifica della Joint Degree
   =============================== */

/* Verifica se la distribuzione di gradi congiunti (nkk) è realizzabile.
   nkk è una GHashTable<int, GHashTable<int,int>>.
*/
int is_valid_joint_degree(GHashTable *nkk) {
    GHashTable *nk = g_hash_table_new(g_direct_hash, g_direct_equal);
    /* Condizione 2: (somma di nkk[k][l]) / k deve essere intero */
    {
        GHashTableIter iter;
        gpointer key, value;
        g_hash_table_iter_init(&iter, nkk);
        while (g_hash_table_iter_next(&iter, &key, &value)) {
            int k = GPOINTER_TO_INT(key);
            int s = 0;
            GHashTable *inner = (GHashTable *) value;
            GHashTableIter inner_iter;
            gpointer ikey, ivalue;
            g_hash_table_iter_init(&inner_iter, inner);
            while (g_hash_table_iter_next(&inner_iter, &ikey, &ivalue))
                s += GPOINTER_TO_INT(ivalue);
            if (k != 0) {
                if (s % k != 0) {
                    printf("Violazione della condizione 2\n");
                    g_hash_table_destroy(nk);
                    return 0;
                }
                g_hash_table_insert(nk, GINT_TO_POINTER(k), GINT_TO_POINTER(s / k));
            } else {
                g_hash_table_insert(nk, GINT_TO_POINTER(k), GINT_TO_POINTER(s));
            }
        }
    }
    /* Condizioni 3, 4, 5 */
    {
        GHashTableIter iter;
        gpointer key, value;
        g_hash_table_iter_init(&iter, nkk);
        while (g_hash_table_iter_next(&iter, &key, &value)) {
            int k = GPOINTER_TO_INT(key);
            GHashTable *inner = (GHashTable *) value;
            GHashTableIter inner_iter;
            gpointer ikey, ivalue;
            g_hash_table_iter_init(&inner_iter, inner);
            while (g_hash_table_iter_next(&inner_iter, &ikey, &ivalue)) {
                int l = GPOINTER_TO_INT(ikey);
                int nkk_val = GPOINTER_TO_INT(ivalue);
                int nk_k = GPOINTER_TO_INT(g_hash_table_lookup(nk, GINT_TO_POINTER(k)));
                int nk_l = GPOINTER_TO_INT(g_hash_table_lookup(nk, GINT_TO_POINTER(l)));
                if (k != l) {
                    if (nkk_val > nk_k * nk_l) {
                        printf("Violazione della condizione 3\n");
                        g_hash_table_destroy(nk);
                        return 0;
                    }
                } else {
                    if (nkk_val > nk_k * (nk_k - 1)) {
                        printf("Violazione della condizione 4\n");
                        g_hash_table_destroy(nk);
                        return 0;
                    }
                    if (nkk_val % 2 != 0) {
                        printf("Violazione della condizione 5\n");
                        g_hash_table_destroy(nk);
                        return 0;
                    }
                }
            }
        }
    }
    g_hash_table_destroy(nk);
    return 1;
}

/* ===============================
   3) neighbor_switch (ottimizzata)
   =============================== */
/* Libera uno stub dal nodo w effettuando uno scambio di arco.
   - node_list: GArray* contenente i nodi dello stesso gruppo di grado.
   - node_residual: array degli stub liberi per ogni nodo.
   - avoid_node_id: se diverso da NO_AVOID, evita quel nodo (se possibile).
*/
void neighbor_switch(FastGraph *g, int w, GArray *node_list, int *node_residual,
                     int avoid_node_id) {
    int w_prime = -1;
    /* Passo 1: scegli w_prime con node_residual[w_prime] > 0 */
    if (avoid_node_id == NO_AVOID || node_residual[avoid_node_id] > 1) {
        for (guint i = 0; i < node_list->len; i++) {
            int cand = g_array_index(node_list, int, i);
            if (node_residual[cand] > 0) {
                w_prime = cand;
                break;
            }
        }
    } else {
        for (guint i = 0; i < node_list->len; i++) {
            int cand = g_array_index(node_list, int, i);
            if (cand != avoid_node_id && node_residual[cand] > 0) {
                w_prime = cand;
                break;
            }
        }
    }
    if (w_prime < 0) {
        fprintf(stderr, "Errore: neighbor_switch: nessun w_prime trovato per il nodo %d\n", w);
        return;
    }
    /* Passo 2: scegli un vicino t di w che non sia adiacente a w_prime */
    int n_neigh;
    int *neighbors = fastgraph_neighbors(g, w, &n_neigh);
    if (!neighbors) {
        fprintf(stderr, "Errore: neighbor_switch: impossibile ottenere i vicini di w=%d\n", w);
        return;
    }
    int t = -1;
    for (int i = 0; i < n_neigh; i++) {
        int cand = neighbors[i];
        if (cand == w_prime) continue;
        if (!fastgraph_has_edge(g, w_prime, cand)) {
            t = cand;
            break;
        }
    }
    free(neighbors);
    if (t < 0) {
        fprintf(stderr, "Errore: neighbor_switch: nessun t valido trovato per w=%d\n", w);
        return;
    }
    /* Passo 3: rimuovi (w,t) e aggiungi (w_prime,t) */
    fastgraph_remove_edge(g, w, t);
    fastgraph_add_edge(g, w_prime, t);
    /* Passo 4: aggiorna gli stub residui */
    node_residual[w] += 1;
    node_residual[w_prime] -= 1;
}

/* ===============================
   4) joint_degree_model
   =============================== */
/* Costruisce il grafo a partire da nkk utilizzando:
      - la matrice di adiacenza,
      - l'array node_residual,
      - la funzione neighbor_switch,
      - e accumulando gli archi in un igraph_vector_int_t.
   Il grafo risultante viene memorizzato in un FastGraph.
*/
void joint_degree_model(GHashTable *nkk, FastGraph *g, igraph_vector_int_t *edge_list) {
    printf("joint_degree_model\n");
    if (!is_valid_joint_degree(nkk)) {
        printf("La distribuzione nkk non è realizzabile come grafo semplice.\n");
        return;
    }
    /* Costruisce "nk": per ogni grado k, quanti nodi hanno quel grado. */
    GHashTable *nk = g_hash_table_new(g_direct_hash, g_direct_equal);
    {
        GHashTableIter iter;
        gpointer key, value;
        g_hash_table_iter_init(&iter, nkk);
        while (g_hash_table_iter_next(&iter, &key, &value)) {
            int k = GPOINTER_TO_INT(key);
            int s = 0;
            GHashTable *inner = (GHashTable *) value;
            GHashTableIter i2;
            gpointer k2, v2;
            g_hash_table_iter_init(&i2, inner);
            while (g_hash_table_iter_next(&i2, &k2, &v2))
                s += GPOINTER_TO_INT(v2);
            if (k != 0)
                g_hash_table_insert(nk, GINT_TO_POINTER(k), GINT_TO_POINTER(s / k));
            else
                g_hash_table_insert(nk, GINT_TO_POINTER(k), GINT_TO_POINTER(s));
        }
    }
    /* Costruisce le liste di nodi per ciascun grado (h_degree_nodelist) e calcola total_nodes. */
    GHashTable *h_degree_nodelist = g_hash_table_new(g_direct_hash, g_direct_equal);
    int total_nodes = 0;
    {
        GHashTableIter iter;
        gpointer key, value;
        g_hash_table_iter_init(&iter, nk);
        while (g_hash_table_iter_next(&iter, &key, &value)) {
            int degree = GPOINTER_TO_INT(key);
            int count  = GPOINTER_TO_INT(value);
            GArray *arr = g_array_new(FALSE, FALSE, sizeof(int));
            for (int v = total_nodes; v < total_nodes + count; v++) {
                g_array_append_val(arr, v);
            }
            g_hash_table_insert(h_degree_nodelist, GINT_TO_POINTER(degree), arr);
            total_nodes += count;
        }
    }
    /* Inizializza il FastGraph con total_nodes. */
    if (fastgraph_init(g, total_nodes) != 0) {
        fprintf(stderr, "Errore: impossibile inizializzare il grafo con %d nodi\n", total_nodes);
        g_hash_table_destroy(nk);
        g_hash_table_destroy(h_degree_nodelist);
        return;
    }
    /* Alloca l'array node_residual. */
    int *node_residual = malloc(total_nodes * sizeof(int));
    if (!node_residual) {
        fprintf(stderr, "Errore: impossibile allocare l'array node_residual\n");
        fastgraph_destroy(g);
        g_hash_table_destroy(nk);
        g_hash_table_destroy(h_degree_nodelist);
        return;
    }
    /* Per ogni nodo, assegna node_residual = grado. */
    {
        GHashTableIter iter;
        gpointer key, value;
        g_hash_table_iter_init(&iter, h_degree_nodelist);
        while (g_hash_table_iter_next(&iter, &key, &value)) {
            int degree = GPOINTER_TO_INT(key);
            GArray *arr = (GArray *) value;
            for (guint i = 0; i < arr->len; i++) {
                int node = g_array_index(arr, int, i);
                node_residual[node] = degree;
            }
        }
    }
    /* Collega l'array node_residual a g per neighbor_switch. */
    g->node_residual = node_residual;
    
    int E = 0;          /* numero di archi aggiunti */
    int n_switches = 0; /* numero di neighbor switch effettuati */
    
    /* Per ogni coppia (k,l) in nkk, aggiunge il numero specificato di archi. */
    {
        GHashTableIter outer_iter;
        gpointer outer_key, outer_value;
        g_hash_table_iter_init(&outer_iter, nkk);
        while (g_hash_table_iter_next(&outer_iter, &outer_key, &outer_value)) {
            int k = GPOINTER_TO_INT(outer_key);
            GHashTable *inner = (GHashTable *) outer_value;
            GHashTableIter inner_iter;
            gpointer inner_key, inner_val;
            g_hash_table_iter_init(&inner_iter, inner);
            while (g_hash_table_iter_next(&inner_iter, &inner_key, &inner_val)) {
                int l = GPOINTER_TO_INT(inner_key);
                int n_edges_add = GPOINTER_TO_INT(inner_val);
                if (n_edges_add > 0 && k >= l) {
                    GArray *k_nodes = g_hash_table_lookup(h_degree_nodelist, GINT_TO_POINTER(k));
                    GArray *l_nodes = g_hash_table_lookup(h_degree_nodelist, GINT_TO_POINTER(l));
                    if (!k_nodes || !l_nodes) continue;
                    int k_size = k_nodes->len;
                    int l_size = l_nodes->len;
                    if (k == l)
                        n_edges_add /= 2;
                    while (n_edges_add > 0) {
                        int v = g_array_index(k_nodes, int, rand() % k_size);
                        int w = g_array_index(l_nodes, int, rand() % l_size);
                        if (v == w) continue;
                        if (!fastgraph_has_edge(g, v, w)) {
                            if (node_residual[v] == 0) {
                                neighbor_switch(g, v, k_nodes, node_residual, NO_AVOID);
                                n_switches++;
                            }
                            if (node_residual[w] == 0) {
                                if (k != l)
                                    neighbor_switch(g, w, l_nodes, node_residual, NO_AVOID);
                                else
                                    neighbor_switch(g, w, k_nodes, node_residual, v);
                                n_switches++;
                            }
                            fastgraph_add_edge(g, v, w);
                            /* Aggiungi l'arco all'edge list igraph (in modalità push_back). */
                            igraph_vector_int_push_back(edge_list, v);
                            igraph_vector_int_push_back(edge_list, w);
                            E++;
                            node_residual[v]--;
                            node_residual[w]--;
                            n_edges_add--;
                        }
                    }
                }
            }
        }
    }
    
    printf("#Switches:%d\n", n_switches);
    printf("#Edges:%d\n", E);
    printf("#Nodes:%d\n", total_nodes);
    
    free(node_residual);
    g->node_residual = NULL;
    g_hash_table_destroy(nk);
    {
        GHashTableIter iter;
        gpointer key, value;
        g_hash_table_iter_init(&iter, h_degree_nodelist);
        while (g_hash_table_iter_next(&iter, &key, &value)) {
            GArray *arr = (GArray *) value;
            g_array_free(arr, TRUE);
        }
    }
    g_hash_table_destroy(h_degree_nodelist);
}

/* ===============================
   5) Funzioni di I/O
   =============================== */

/* load_nkk:
   Legge un file con righe "k,l,value" e popola nkk (hash table: k -> (hash table: l->value)).
*/
void load_nkk(char *fname, GHashTable *nkk) {
    FILE *fp = fopen(fname, "r");
    if (!fp) {
        fprintf(stderr, "Errore: impossibile aprire il file %s\n", fname);
        return;
    }
    printf("Caricamento file %s\n", fname);
    char line[1024];
    while (fgets(line, sizeof(line), fp)) {
        char *nl = strchr(line, '\n');
        if (nl) *nl = '\0';
        int k, l, val;
        if (sscanf(line, "%d,%d,%d", &k, &l, &val) == 3) {
            GHashTable *inner = g_hash_table_lookup(nkk, GINT_TO_POINTER(k));
            if (!inner) {
                inner = g_hash_table_new(g_direct_hash, g_direct_equal);
                g_hash_table_insert(nkk, GINT_TO_POINTER(k), inner);
            }
            g_hash_table_insert(inner, GINT_TO_POINTER(l), GINT_TO_POINTER(val));
        } else {
            printf("Errore nel caricamento della riga: %s\n", line);
        }
    }
    printf("  Fatto.\n");
    fclose(fp);
}

/* write_graph:
   Scrive gli archi di g in formato edge list "u,v" per riga.
   Esegue un doppio ciclo sulla matrice di adiacenza e scrive solo (u,v) con u < v.
*/
void write_graph(char *fname, const FastGraph *g) {
    FILE *fp = fopen(fname, "w");
    if (!fp) {
        fprintf(stderr, "Errore: impossibile aprire il file %s per scrittura.\n", fname);
        return;
    }
    printf("Scrittura del file %s.\n", fname);
    int E = 0;
    int n = g->total_nodes;
    for (int u = 0; u < n; u++) {
        for (int v = u + 1; v < n; v++) {
            if (fastgraph_has_edge(g, u, v)) {
                fprintf(fp, "%d,%d\n", u, v);
                E++;
            }
        }
    }
    fclose(fp);
    printf("%d archi. Fatto.\n", E);
}

/* ===============================
   6) Conversione in igraph (Ibrida)
   =============================== */
/* Converte il FastGraph (costruito velocemente) in un grafo igraph.
   Qui utilizziamo l'edge list accumulata nell'igraph_vector_int_t.
*/
void convert_to_igraph(const FastGraph *g, igraph_t *igraph_graph, const igraph_vector_int_t *edge_list) {
    int n = g->total_nodes;
    /* Usa igraph_create per creare il grafo in un'unica chiamata */
    if (igraph_create(igraph_graph, edge_list, n, IGRAPH_UNDIRECTED) != IGRAPH_SUCCESS) {
        fprintf(stderr, "Errore: igraph_create fallita.\n");
        return;
    }
}

/* ===============================
   7) Funzione main
   =============================== */

int main(int argc, char *argv[]) {
    srand((unsigned) time(NULL));
    if (argc < 2) {
        fprintf(stderr, "Uso: %s <file.nkk>\n", argv[0]);
        return 1;
    }
    char *fname = argv[1];

    /* Crea la distribuzione dei gradi congiunti nkk come una GHashTable. */
    GHashTable *nkk = g_hash_table_new(g_direct_hash, g_direct_equal);
    load_nkk(fname, nkk);

    printf("Esecuzione della costruzione\n");
    struct timeval tp1, tp2;
    gettimeofday(&tp1, NULL);

    /* Prepara l'edge list igraph: si stima (o si lascia espandere) la capacità dinamica */
    igraph_vector_int_t edge_list;
    igraph_vector_int_init(&edge_list, 0);  // inizialmente vuoto

    /* Costruisce il grafo e accumula gli archi in edge_list */
    FastGraph fast_g;
    fast_g.total_nodes = 0;
    fast_g.adj_matrix = NULL;
    fast_g.node_residual = NULL;
    joint_degree_model(nkk, &fast_g, &edge_list);

    gettimeofday(&tp2, NULL);
    double runtime = ((tp2.tv_sec - tp1.tv_sec) * 1000000 + (tp2.tv_usec - tp1.tv_usec)) / 1e6;
    printf("Tempo:%.3f secondi\n", runtime);

    /* Converte il FastGraph in un grafo igraph usando l'edge list accumulata */
    igraph_t ig_graph;
    convert_to_igraph(&fast_g, &ig_graph, &edge_list);
    printf("Grafo igraph creato con %d nodi.\n", (int)igraph_vcount(&ig_graph));

    /* Scrive il grafo su file in formato edge list. */
    write_graph("generated.graph", &fast_g);
    printf("Grafo 'generated.graph' generato in formato edge list\n");

    /* Pulizia finale. */
    fastgraph_destroy(&fast_g);
    igraph_vector_int_destroy(&edge_list);
    {
        GHashTableIter iter;
        gpointer key, value;
        g_hash_table_iter_init(&iter, nkk);
        while (g_hash_table_iter_next(&iter, &key, &value)) {
            GHashTable *inner = (GHashTable *) value;
            g_hash_table_destroy(inner);
        }
    }
    g_hash_table_destroy(nkk);

    igraph_destroy(&ig_graph);

    return 0;
}
