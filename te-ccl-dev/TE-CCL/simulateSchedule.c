#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "cJSON.h"

#define MAX_PATHS 2000
#define MAX_HOPS 20
#define MAX_STR_LEN 128

typedef struct {
    int src;
    int dst;
    int epoch;
} Hop;

typedef struct {
    int demand;
    int chunk;
    int origin;
    int met_epoch;
    int num_hops;
    Hop hops[MAX_HOPS];
} ChunkPath;

ChunkPath paths[MAX_PATHS];
int num_paths = 0;

void add_path(int demand, int chunk, int origin, int met_epoch, int num_hops, Hop* hops) {
    if (num_paths >= MAX_PATHS) {
        fprintf(stderr, "Limite paths superato\n");
        exit(EXIT_FAILURE);
    }
    paths[num_paths].demand = demand;
    paths[num_paths].chunk = chunk;
    paths[num_paths].origin = origin;
    paths[num_paths].met_epoch = met_epoch;
    paths[num_paths].num_hops = num_hops;
    for (int i = 0; i < num_hops; i++) {
        paths[num_paths].hops[i] = hops[i];
    }
    num_paths++;
}

void parse_hop_string(const char* hop_str, Hop* hop) {
    sscanf(hop_str, "%d->%d in epoch %d", &hop->src, &hop->dst, &hop->epoch);
}

void parse_key(const char* key, int* demand, int* chunk, int* origin, int* met_epoch) {
    sscanf(key, "Demand at %d for chunk %d from %d met by epoch %d", demand, chunk, origin, met_epoch);
}

void compare_with_mpi_allgather(int rank, int size, int data_size, int* my_data, int* simulated_allgather) {
    int total_data_size = data_size * size;
    int* mpi_allgather_buffer = malloc(total_data_size * sizeof(int));
    if (!mpi_allgather_buffer) {
        fprintf(stderr, "Rank %d: Errore allocazione memoria per mpi_allgather_buffer\n", rank);
        MPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE);
    }

    MPI_Allgather(my_data, data_size, MPI_INT, mpi_allgather_buffer, data_size, MPI_INT, MPI_COMM_WORLD);

    int mismatch = 0;
    for (int i = 0; i < total_data_size; i++) {
        if (mpi_allgather_buffer[i] != simulated_allgather[i]) {
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

    free(mpi_allgather_buffer);
}

void load_paths_from_json(const char* schedule_file) {
    FILE* fp = fopen(schedule_file, "r");
    if (!fp) {
        perror("Errore apertura file schedule");
        exit(EXIT_FAILURE);
    }
    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    char* json_data = (char*)malloc(size + 1);
    fread(json_data, 1, size, fp);
    fclose(fp);
    json_data[size] = '\0';

    cJSON* root = cJSON_Parse(json_data);
    if (!root) {
        fprintf(stderr, "Errore parsing JSON schedule\n");
        free(json_data);
        exit(EXIT_FAILURE);
    }

    cJSON* chunk_paths = cJSON_GetObjectItemCaseSensitive(root, "8-Chunk paths");
    if (!chunk_paths) {
        fprintf(stderr, "\"8-Chunk paths\" non trovato nel JSON!\n");
        cJSON_Delete(root);
        free(json_data);
        exit(EXIT_FAILURE);
    }

    cJSON* path_entry = NULL;
    cJSON_ArrayForEach(path_entry, chunk_paths) {
        if (!path_entry->string) {
            fprintf(stderr, "Chiave percorso mancante\n");
            cJSON_Delete(root);
            free(json_data);
            exit(EXIT_FAILURE);
        }
        const char* key = path_entry->string;
        int demand, chunk, origin, met_epoch;
        parse_key(key, &demand, &chunk, &origin, &met_epoch);

        int num_hops = cJSON_GetArraySize(path_entry);
        Hop hops[MAX_HOPS];
        for (int i = 0; i < num_hops; i++) {
            cJSON* hop_item = cJSON_GetArrayItem(path_entry, i);
            if (!hop_item || !hop_item->valuestring) {
                fprintf(stderr, "Hop string mancante\n");
                cJSON_Delete(root);
                free(json_data);
                exit(EXIT_FAILURE);
            }
            parse_hop_string(hop_item->valuestring, &hops[i]);
        }
        add_path(demand, chunk, origin, met_epoch, num_hops, hops);
    }

    cJSON_Delete(root);
    free(json_data);
}

void parse_config(const char* config_file, int* leaf_routers, int* hosts_per_router, int* num_chunks, int* num_epochs, int* chunk_size) {
    FILE* fp = fopen(config_file, "r");
    if (!fp) {
        perror("Errore apertura file config");
        exit(EXIT_FAILURE);
    }
    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    char* json_data = (char*)malloc(size + 1);
    fread(json_data, 1, size, fp);
    fclose(fp);
    json_data[size] = '\0';

    cJSON* root = cJSON_Parse(json_data);
    if (!root) {
        fprintf(stderr, "Errore parsing JSON config\n");
        free(json_data);
        exit(EXIT_FAILURE);
    }

    cJSON* topology = cJSON_GetObjectItemCaseSensitive(root, "TopologyParams");
    *leaf_routers = cJSON_GetObjectItemCaseSensitive(topology, "leaf_routers")->valueint;
    *hosts_per_router = cJSON_GetObjectItemCaseSensitive(topology, "hosts_per_router")->valueint;
    *chunk_size = cJSON_GetObjectItemCaseSensitive(topology, "chunk_size")->valueint;

    cJSON* instance = cJSON_GetObjectItemCaseSensitive(root, "InstanceParams");
    *num_chunks = cJSON_GetObjectItemCaseSensitive(instance, "num_chunks")->valueint;
    *num_epochs = cJSON_GetObjectItemCaseSensitive(instance, "num_epochs")->valueint;

    cJSON_Delete(root);
    free(json_data);
}

void simulate_epoch(int rank, int epoch, int chunk_int_size, int* simulated_allgather, int data_size) {
    for (int i = 0; i < num_paths; i++) {
        ChunkPath* p = &paths[i];
        for (int h = 0; h < p->num_hops; h++) {
            Hop* hop = &p->hops[h];
            if (hop->epoch != epoch) continue;
            if (hop->src == hop->dst) continue;

            int* origin_chunk_ptr = simulated_allgather + p->origin * data_size + p->chunk * chunk_int_size;

            if (rank == hop->src) {
                MPI_Send(origin_chunk_ptr, chunk_int_size, MPI_INT, hop->dst, epoch, MPI_COMM_WORLD);
            } else if (rank == hop->dst) {
                int* recv_chunk = malloc(chunk_int_size * sizeof(int));
                MPI_Recv(recv_chunk, chunk_int_size, MPI_INT, hop->src, epoch, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

                memcpy(simulated_allgather + p->origin * data_size + p->chunk * chunk_int_size,
                       recv_chunk, chunk_int_size * sizeof(int));
                free(recv_chunk);
            }
        }
    }
}

int main(int argc, char** argv) {
    MPI_Init(&argc, &argv);
    int rank, size;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);

    if (argc != 3) {
        if (rank == 0) fprintf(stderr, "Utilizzo: %s <config_json> <schedule_json>\n", argv[0]);
        MPI_Finalize(); return EXIT_FAILURE;
    }

    int leaf_routers, hosts_per_router, num_chunks, num_epochs, chunk_size;

    if (rank == 0) {
        parse_config(argv[1], &leaf_routers, &hosts_per_router, &num_chunks, &num_epochs, &chunk_size);
        load_paths_from_json(argv[2]);
    }

    MPI_Bcast(&leaf_routers, 1, MPI_INT, 0, MPI_COMM_WORLD);
    MPI_Bcast(&hosts_per_router, 1, MPI_INT, 0, MPI_COMM_WORLD);
    MPI_Bcast(&num_chunks, 1, MPI_INT, 0, MPI_COMM_WORLD);
    MPI_Bcast(&num_epochs, 1, MPI_INT, 0, MPI_COMM_WORLD);
    MPI_Bcast(&chunk_size, 1, MPI_INT, 0, MPI_COMM_WORLD);
    MPI_Bcast(&num_paths, 1, MPI_INT, 0, MPI_COMM_WORLD);
    MPI_Bcast(paths, sizeof(ChunkPath) * num_paths, MPI_BYTE, 0, MPI_COMM_WORLD);

    int total_processes = leaf_routers * hosts_per_router + 1;
    if (size != total_processes) {
        if (rank == 0)
            fprintf(stderr, "Errore: numero processi MPI forniti (%d) non corrisponde a quello richiesto (%d)!\n", size, total_processes);
        MPI_Finalize(); return EXIT_FAILURE;
    }

    int chunk_int_size = chunk_size / sizeof(int);
    int data_size = num_chunks * chunk_int_size;
    int* my_data = malloc(data_size * sizeof(int));
    srand(time(NULL) + rank * 100);
    for (int i = 0; i < data_size; i++) my_data[i] = rand();

    int total_data_size = data_size * size;
    int* simulated_allgather = malloc(total_data_size * sizeof(int));
    memcpy(simulated_allgather + rank * data_size, my_data, data_size * sizeof(int));

    if (rank == 0) printf("Inizio simulazione con %d epoche...\n", num_epochs);

    printf("Rank %d - Array iniziale my_data[]:\n", rank);
    for (int i = 0; i < data_size; i++) {
        printf("%d ", my_data[i]);
    }
    printf("\n");
    fflush(stdout);


    int max_epoch = 0;
    for (int i = 0; i < num_paths; i++) {
        for (int h = 0; h < paths[i].num_hops; h++) {
            if (paths[i].hops[h].epoch > max_epoch) max_epoch = paths[i].hops[h].epoch;
        }
    }

    MPI_Barrier(MPI_COMM_WORLD);
    double t_start = MPI_Wtime();
    for (int epoch = 0; epoch <= max_epoch; epoch++) {
        simulate_epoch(rank, epoch, chunk_int_size, simulated_allgather, data_size);
    }
    MPI_Barrier(MPI_COMM_WORLD);
    double t_end = MPI_Wtime();

    compare_with_mpi_allgather(rank, size, data_size, my_data, simulated_allgather);

    MPI_Barrier(MPI_COMM_WORLD);
    printf("Rank %d - Array finale simulated_allgather[]:\n", rank);
    for (int i = 0; i < total_data_size; i++) {
        printf("%d ", simulated_allgather[i]);
    }
    printf("\n");
    fflush(stdout);


    if (rank == 0) {
        printf("Tempo totale simulazione: %.6f secondi\n", t_end - t_start);
    }

    free(my_data);
    free(simulated_allgather);
    MPI_Finalize();
    return 0;
}
