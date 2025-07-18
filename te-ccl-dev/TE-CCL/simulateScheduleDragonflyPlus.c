#include <mpi.h>                // Libreria MPI per comunicazioni parallele
#include <stdio.h>              // I/O standard (printf, fprintf, etc.)
#include <stdlib.h>             // Funzioni di utilità (malloc, free, exit, srand, rand)
#include <string.h>             // Funzioni di manipolazione stringhe e memoria (memcpy)
#include <time.h>               // Funzioni tempo (time) per inizializzare il generatore di numeri casuali
#include "cJSON.h"              // Libreria per parsing JSON

#define MAX_PATHS 2000          // Numero massimo di percorsi (paths) gestibili
#define MAX_HOPS 20             // Numero massimo di hop per percorso
#define MAX_STR_LEN 128         // Lunghezza massima stringa (non usata esplicitamente)

typedef struct {
    int src;                    // Nodo sorgente di un hop
    int dst;                    // Nodo destinazione di un hop
    int epoch;                  // Epoca in cui questo hop avviene
} Hop;

typedef struct {
    int demand;                 // Identificativo della domanda
    int chunk;                  // Identificativo del chunk
    int origin;                 // Nodo origine del chunk
    int met_epoch;              // Epoca entro cui la domanda deve essere soddisfatta
    int num_hops;               // Numero di hop nel percorso
    Hop hops[MAX_HOPS];         // Array degli hop che compongono il percorso
} ChunkPath;

ChunkPath paths[MAX_PATHS];     // Array globale che contiene tutti i percorsi caricati
int num_paths = 0;              // Numero di percorsi attualmente caricati

// Funzione per aggiungere un percorso all'array globale paths
void add_path(int demand, int chunk, int origin, int met_epoch, int num_hops, Hop* hops) {
    if (num_paths >= MAX_PATHS) {    // Controlla di non superare il limite massimo
        fprintf(stderr, "Limite paths superato\n");
        exit(EXIT_FAILURE);           // Termina il programma in caso di overflow
    }
    // Imposta i campi del nuovo percorso
    paths[num_paths].demand = demand;
    paths[num_paths].chunk = chunk;
    paths[num_paths].origin = origin;
    paths[num_paths].met_epoch = met_epoch;
    paths[num_paths].num_hops = num_hops;
    // Copia gli hop nel percorso
    for (int i = 0; i < num_hops; i++) {
        paths[num_paths].hops[i] = hops[i];
    }
    num_paths++;                    // Incrementa il contatore globale dei percorsi
}

// Funzione che estrae i dati da una stringa hop nel formato "src->dst in epoch X"
void parse_hop_string(const char* hop_str, Hop* hop) {
    sscanf(hop_str, "%d->%d in epoch %d", &hop->src, &hop->dst, &hop->epoch);
}

// Funzione che estrae i dati da una chiave percorso nel formato "Demand at %d for chunk %d from %d met by epoch %d"
void parse_key(const char* key, int* demand, int* chunk, int* origin, int* met_epoch) {
    sscanf(key, "Demand at %d for chunk %d from %d met by epoch %d", demand, chunk, origin, met_epoch);
}

// Funzione che confronta il buffer simulato con il risultato di MPI_Allgather
void compare_with_mpi_allgather(int rank, int size, int data_size, int* my_data, int* simulated_allgather) {
    int total_data_size = data_size * size;                // Dimensione totale dati di tutti i processi
    int* mpi_allgather_buffer = malloc(total_data_size * sizeof(int)); // Buffer per risultato MPI_Allgather
    if (!mpi_allgather_buffer) {                            // Controllo allocazione
        fprintf(stderr, "Rank %d: Errore allocazione memoria per mpi_allgather_buffer\n", rank);
        MPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE);           // Termina MPI in caso di errore
    }

    // Esegue MPI_Allgather reale: tutti raccolgono dati di tutti
    MPI_Allgather(my_data, data_size, MPI_INT, mpi_allgather_buffer, data_size, MPI_INT, MPI_COMM_WORLD);

    int mismatch = 0;                                       // Flag per differenze trovate
    for (int i = 0; i < total_data_size; i++) {
        if (mpi_allgather_buffer[i] != simulated_allgather[i]) { // Confronta elemento per elemento
            mismatch = 1;
            printf("Rank %d: Differenza in posizione %d: simulato=%d, MPI=%d\n",
                   rank, i, simulated_allgather[i], mpi_allgather_buffer[i]);
        }
    }

    if (mismatch) {
        printf("Rank %d: ERRORE - Il buffer simulato NON corrisponde a quello di MPI_Allgather!\n", rank);
    } else {
        printf("Rank %d: SUCCESSO - Il buffer simulato corrisponde a quello di MPI_Allgather.\n", rank);
    }

    free(mpi_allgather_buffer);                             // Libera la memoria allocata
}

// Funzione che carica i percorsi da un file JSON schedule
void load_paths_from_json(const char* schedule_file) {
    FILE* fp = fopen(schedule_file, "r");                   // Apre il file in lettura
    if (!fp) {                                              // Controllo apertura file
        perror("Errore apertura file schedule");
        exit(EXIT_FAILURE);
    }
    fseek(fp, 0, SEEK_END);                                 // Sposta il cursore a fine file
    long size = ftell(fp);                                  // Ottiene dimensione file
    fseek(fp, 0, SEEK_SET);                                 // Torna all'inizio
    char* json_data = (char*)malloc(size + 1);              // Alloca buffer per contenuto file + terminatore
    fread(json_data, 1, size, fp);                           // Legge tutto il file nel buffer
    fclose(fp);                                             // Chiude il file
    json_data[size] = '\0';                                 // Aggiunge terminatore stringa

    cJSON* root = cJSON_Parse(json_data);                   // Parsing JSON
    if (!root) {                                            // Controllo parsing
        fprintf(stderr, "Errore parsing JSON schedule\n");
        free(json_data);
        exit(EXIT_FAILURE);
    }

    // Ottiene l'array associato alla chiave "8-Chunk paths"
    cJSON* chunk_paths = cJSON_GetObjectItemCaseSensitive(root, "8-Chunk paths");
    if (!chunk_paths) {
        fprintf(stderr, "\"8-Chunk paths\" non trovato nel JSON!\n");
        cJSON_Delete(root);
        free(json_data);
        exit(EXIT_FAILURE);
    }

    cJSON* path_entry = NULL;
    // Itera su ogni elemento (percorso) dell'array chunk_paths
    cJSON_ArrayForEach(path_entry, chunk_paths) {
        if (!path_entry->string) {                          // Controllo presenza chiave (stringa)
            fprintf(stderr, "Chiave percorso mancante\n");
            cJSON_Delete(root);
            free(json_data);
            exit(EXIT_FAILURE);
        }
        const char* key = path_entry->string;              // Chiave percorso (es. "Demand at ...")
        int demand, chunk, origin, met_epoch;
        parse_key(key, &demand, &chunk, &origin, &met_epoch);// Parsing della chiave per estrarre i parametri

        int num_hops = cJSON_GetArraySize(path_entry);     // Numero di hop per questo percorso
        Hop hops[MAX_HOPS];                                 // Array temporaneo per gli hop
        for (int i = 0; i < num_hops; i++) {
            cJSON* hop_item = cJSON_GetArrayItem(path_entry, i); // Ottiene l'i-esimo hop
            if (!hop_item || !hop_item->valuestring) {     // Controllo presenza stringa hop
                fprintf(stderr, "Hop string mancante\n");
                cJSON_Delete(root);
                free(json_data);
                exit(EXIT_FAILURE);
            }
            parse_hop_string(hop_item->valuestring, &hops[i]); // Parsing della stringa hop
        }
        add_path(demand, chunk, origin, met_epoch, num_hops, hops); // Aggiunge il percorso all'array globale
    }

    cJSON_Delete(root);                                     // Libera memoria cJSON
    free(json_data);                                        // Libera buffer file
}

// Funzione che legge e parse il file di configurazione con parametri topologici DragonflyPlus
void parse_config(const char* config_file, int* num_groups, int* leaf_routers, int* spine_routers,
                  int* hosts_per_router, int* num_chunks, int* num_epochs, int* chunk_size) {
    FILE* fp = fopen(config_file, "r");                     // Apre il file config in lettura
    if (!fp) {
        perror("Errore apertura file config");
        exit(EXIT_FAILURE);
    }
    fseek(fp, 0, SEEK_END);                                 // Posiziona cursore a fine file
    long size = ftell(fp);                                  // Ottiene dimensione file
    fseek(fp, 0, SEEK_SET);                                 // Torna all'inizio
    char* json_data = (char*)malloc(size + 1);              // Alloca buffer per contenuto + terminatore
    fread(json_data, 1, size, fp);                           // Legge tutto il file nel buffer
    fclose(fp);                                             // Chiude il file
    json_data[size] = '\0';                                 // Termina stringa

    cJSON* root = cJSON_Parse(json_data);                   // Parsing JSON
    if (!root) {
        fprintf(stderr, "Errore parsing JSON config\n");
        free(json_data);
        exit(EXIT_FAILURE);
    }

    // Ottiene oggetto "TopologyParams"
    cJSON* topology = cJSON_GetObjectItemCaseSensitive(root, "TopologyParams");
    if (!topology) {
        fprintf(stderr, "\"TopologyParams\" non trovato nel JSON!\n");
        cJSON_Delete(root);
        free(json_data);
        exit(EXIT_FAILURE);
    }
    // Estrae i parametri topologici richiesti
    cJSON* num_groups_json = cJSON_GetObjectItemCaseSensitive(topology, "num_groups");
    cJSON* leaf_routers_json = cJSON_GetObjectItemCaseSensitive(topology, "leaf_routers");
    cJSON* spine_routers_json = cJSON_GetObjectItemCaseSensitive(topology, "spine_routers");
    cJSON* hosts_per_router_json = cJSON_GetObjectItemCaseSensitive(topology, "hosts_per_router");
    cJSON* chunk_size_json = cJSON_GetObjectItemCaseSensitive(topology, "chunk_size");
    if (!num_groups_json || !leaf_routers_json || !spine_routers_json || !hosts_per_router_json || !chunk_size_json) {
        fprintf(stderr, "Parametri mancanti in TopologyParams!\n");
        cJSON_Delete(root);
        free(json_data);
        exit(EXIT_FAILURE);
    }
    // Assegna i valori estratti ai parametri passati per riferimento
    *num_groups = num_groups_json->valueint;
    *leaf_routers = leaf_routers_json->valueint;
    *spine_routers = spine_routers_json->valueint;
    *hosts_per_router = hosts_per_router_json->valueint;
    *chunk_size = chunk_size_json->valueint;

    // Ottiene oggetto "InstanceParams"
    cJSON* instance = cJSON_GetObjectItemCaseSensitive(root, "InstanceParams");
    if (!instance) {
        fprintf(stderr, "\"InstanceParams\" non trovato nel JSON!\n");
        cJSON_Delete(root);
        free(json_data);
        exit(EXIT_FAILURE);
    }
    // Estrae parametri istanza
    cJSON* num_chunks_json = cJSON_GetObjectItemCaseSensitive(instance, "num_chunks");
    cJSON* num_epochs_json = cJSON_GetObjectItemCaseSensitive(instance, "num_epochs");
    if (!num_chunks_json || !num_epochs_json) {
        fprintf(stderr, "Parametri mancanti in InstanceParams!\n");
        cJSON_Delete(root);
        free(json_data);
        exit(EXIT_FAILURE);
    }
    // Assegna i valori estratti
    *num_chunks = num_chunks_json->valueint;
    *num_epochs = num_epochs_json->valueint;

    cJSON_Delete(root);                                     // Libera memoria cJSON
    free(json_data);                                        // Libera buffer file
}

// Funzione che simula un'epoca di comunicazione
void simulate_epoch(int rank, int size, int epoch, int chunk_int_size, int* simulated_allgather, int data_size) {
    // Scorre tutti i percorsi caricati
    for (int i = 0; i < num_paths; i++) {
        ChunkPath* p = &paths[i];                          // Puntatore al percorso corrente
        // Scorre tutti gli hop del percorso
        for (int h = 0; h < p->num_hops; h++) {
            Hop* hop = &p->hops[h];                        // Puntatore all'hop corrente
            if (hop->epoch != epoch) continue;             // Salta se l'epoca non corrisponde a quella simulata
            if (hop->src == hop->dst) continue;            // Ignora hop da nodo a sé stesso

            // Controlla validità dei rank sorgente e destinazione
            if (hop->src < 0 || hop->src >= size || hop->dst < 0 || hop->dst >= size) {
                fprintf(stderr, "Rank %d: Errore - src (%d) o dst (%d) fuori range [0, %d)\n",
                        rank, hop->src, hop->dst, size);
                MPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE);  // Termina MPI in caso di errore
            }

            // Calcola puntatore al chunk di origine nel buffer simulato
            int* origin_chunk_ptr = simulated_allgather + p->origin * data_size + p->chunk * chunk_int_size;

            if (rank == hop->src) {
                // Se sono il nodo sorgente, invio il chunk al nodo destinazione
                MPI_Send(origin_chunk_ptr, chunk_int_size, MPI_INT, hop->dst, epoch, MPI_COMM_WORLD);
            } else if (rank == hop->dst) {
                // Se sono il nodo destinazione, ricevo il chunk dal nodo sorgente
                int* recv_chunk = malloc(chunk_int_size * sizeof(int));
                MPI_Recv(recv_chunk, chunk_int_size, MPI_INT, hop->src, epoch, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

                // Aggiorno il buffer simulato con i dati ricevuti
                memcpy(simulated_allgather + p->origin * data_size + p->chunk * chunk_int_size,
                       recv_chunk, chunk_int_size * sizeof(int));
                free(recv_chunk);                          // Libero memoria temporanea
            }
        }
    }
}

int main(int argc, char** argv) {
    MPI_Init(&argc, &argv);                                // Inizializza ambiente MPI
    int rank, size;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);                  // Ottiene rank del processo corrente
    MPI_Comm_size(MPI_COMM_WORLD, &size);                  // Ottiene numero totale processi MPI

    if (argc != 3) {                                       // Controlla numero argomenti
        if (rank == 0) fprintf(stderr, "Utilizzo: %s <config_json> <schedule_json>\n", argv[0]);
        MPI_Finalize();                                    // Finalizza MPI
        return EXIT_FAILURE;
    }

    int num_groups, leaf_routers, spine_routers, hosts_per_router;
    int num_chunks, num_epochs, chunk_size;

    if (rank == 0) {
        // Processo 0 legge la configurazione e lo schedule dai file JSON
        parse_config(argv[1], &num_groups, &leaf_routers, &spine_routers, &hosts_per_router,
                     &num_chunks, &num_epochs, &chunk_size);
        load_paths_from_json(argv[2]);
    }

    // Diffonde i parametri a tutti i processi MPI
    MPI_Bcast(&num_groups, 1, MPI_INT, 0, MPI_COMM_WORLD);
    MPI_Bcast(&leaf_routers, 1, MPI_INT, 0, MPI_COMM_WORLD);
    MPI_Bcast(&spine_routers, 1, MPI_INT, 0, MPI_COMM_WORLD);
    MPI_Bcast(&hosts_per_router, 1, MPI_INT, 0, MPI_COMM_WORLD);
    MPI_Bcast(&num_chunks, 1, MPI_INT, 0, MPI_COMM_WORLD);
    MPI_Bcast(&num_epochs, 1, MPI_INT, 0, MPI_COMM_WORLD);
    MPI_Bcast(&chunk_size, 1, MPI_INT, 0, MPI_COMM_WORLD);
    MPI_Bcast(&num_paths, 1, MPI_INT, 0, MPI_COMM_WORLD);
    MPI_Bcast(paths, sizeof(ChunkPath) * num_paths, MPI_BYTE, 0, MPI_COMM_WORLD);

    // Calcola numero totale processi richiesti dalla topologia DragonflyPlus
    int r = leaf_routers + spine_routers;                  // Numero router per gruppo
    int total_processes = num_groups * r * hosts_per_router; // Numero totale processi richiesti

    // Controlla che il numero di processi MPI lanciati corrisponda a quello richiesto
    if (size != total_processes) {
        if (rank == 0) {
            fprintf(stderr, "Errore: numero processi MPI forniti (%d) non corrisponde a quello richiesto dalla topologia DragonflyPlus (%d)!\n",
                    size, total_processes);
        }
        MPI_Finalize();
        return EXIT_FAILURE;
    }

    // Calcola dimensione chunk in interi (chunk_size è in byte)
    int chunk_int_size = chunk_size / sizeof(int);
    int data_size = num_chunks * chunk_int_size;

    // Alloca array locale dati e lo inizializza con valori casuali
    int* my_data = malloc(data_size * sizeof(int));
    srand(time(NULL) + rank * 100);                         // Seed random differente per rank
    for (int i = 0; i < data_size; i++) my_data[i] = rand();

    // Alloca buffer simulato per tutti i dati di tutti i processi
    int total_data_size = data_size * size;
    int* simulated_allgather = malloc(total_data_size * sizeof(int));
    // Copia i dati locali nella posizione corretta del buffer simulato
    memcpy(simulated_allgather + rank * data_size, my_data, data_size * sizeof(int));

    if (rank == 0) printf("Inizio simulazione con %d epoche...\n", num_epochs);

    // Stampa array iniziale dati locali (per debug)
    printf("Rank %d - Array iniziale my_data[]:\n", rank);
    for (int i = 0; i < data_size; i++) {
        printf("%d ", my_data[i]);
    }
    printf("\n");
    fflush(stdout);

    // Trova epoca massima tra tutti gli hop caricati
    int max_epoch = 0;
    for (int i = 0; i < num_paths; i++) {
        for (int h = 0; h < paths[i].num_hops; h++) {
            if (paths[i].hops[h].epoch > max_epoch) max_epoch = paths[i].hops[h].epoch;
        }
    }

    MPI_Barrier(MPI_COMM_WORLD);                           // Sincronizza tutti i processi
    double t_start = MPI_Wtime();                           // Tempo inizio simulazione

    // Simula tutte le epoche da 0 a max_epoch
    for (int epoch = 0; epoch <= max_epoch; epoch++) {
        simulate_epoch(rank, size, epoch, chunk_int_size, simulated_allgather, data_size);
    }

    MPI_Barrier(MPI_COMM_WORLD);                           // Sincronizza tutti i processi
    double t_end = MPI_Wtime();                             // Tempo fine simulazione

    // Confronta buffer simulato con risultato MPI_Allgather
    compare_with_mpi_allgather(rank, size, data_size, my_data, simulated_allgather);

    MPI_Barrier(MPI_COMM_WORLD);                           // Sincronizza tutti i processi

    // Stampa array finale simulato (per debug)
    printf("Rank %d - Array finale simulated_allgather[]:\n", rank);
    for (int i = 0; i < total_data_size; i++) {
        printf("%d ", simulated_allgather[i]);
    }
    printf("\n");
    fflush(stdout);

    if (rank == 0) {
        printf("Tempo totale simulazione: %.6f secondi\n", t_end - t_start);
    }

    // Libera memoria allocata
    free(my_data);
    free(simulated_allgather);
    MPI_Finalize();                                        // Finalizza ambiente MPI
    return 0;                                              // Termina programma con successo
}
