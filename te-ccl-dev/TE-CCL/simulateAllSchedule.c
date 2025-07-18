#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "cJSON.h"

// Struct della configurazione aggregata da broadcastare
typedef struct {
    int num_groups, leaf_routers, spine_routers, hosts_per_router,
        num_chunks, num_epochs, chunk_size;
} ConfigParams;

// Struct hop
typedef struct {
    int demand;
    int chunk;
    int origin;
    int met_epoch;
    struct { int src, dst, epoch; } hop;
} RankHop;

// Lista hop solo locale (per ogni processo)
RankHop* rank_hops = NULL;
int num_rank_hops = 0;
int max_rank_hops = 0;

// Gestione dinamica hop
void add_rank_hop(int demand, int chunk, int origin, int met_epoch, int src, int dst, int epoch, int my_rank) {
    if (!(src==my_rank || dst==my_rank)) return;
    if (num_rank_hops >= max_rank_hops) {
        max_rank_hops = max_rank_hops ? max_rank_hops*2 : 32;
        rank_hops = realloc(rank_hops, max_rank_hops * sizeof(RankHop));
        if (!rank_hops) { fprintf(stderr,"Error realloc rank_hops\n"); MPI_Abort(MPI_COMM_WORLD, 1); }
    }
    RankHop* rh = &rank_hops[num_rank_hops++];
    rh->demand = demand;
    rh->chunk = chunk;
    rh->origin = origin;
    rh->met_epoch = met_epoch;
    rh->hop.src = src;
    rh->hop.dst = dst;
    rh->hop.epoch = epoch;
}

// Serializzazione/deserializzazione RankHop per invio via MPI
char* serialize_ranks(const RankHop* vec, int n, int *out_size) {
    int chunk = 7*sizeof(int);
    *out_size = sizeof(int) + n*chunk;
    char* buf = malloc(*out_size);
    memcpy(buf, &n, sizeof(int));
    for (int i=0; i<n; ++i)
        memcpy(buf + sizeof(int)+i*chunk, &vec[i], chunk);
    return buf;
}
RankHop* deserialize_ranks(const char* buf, int* nout) {
    int n;
    memcpy(&n, buf, sizeof(int));
    *nout = n;
    if (n==0) return NULL;
    int chunk = 7*sizeof(int);
    RankHop* r = malloc(n*sizeof(RankHop));
    for (int i=0; i<n; ++i)
        memcpy(&r[i], buf+sizeof(int)+i*chunk, chunk);
    return r;
}

// Helpers parsing
void parse_hop_string(const char* s, int* src, int* dst, int* epoch) {
    sscanf(s, "%d->%d in epoch %d", src, dst, epoch);
}
void parse_key(const char* k, int* demand, int* chunk, int* origin, int* met_epoch) {
    sscanf(k, "Demand at %d for chunk %d from %d met by epoch %d", demand, chunk, origin, met_epoch);
}

// SOLO RANK 0: parsa e distribuisce i percorsi agli altri rank
void distrib_parse_json_and_scatter(const char* filename, int size, int my_rank) {
    FILE* fp = fopen(filename, "r");
    if (!fp) { perror("open schedule"); MPI_Abort(MPI_COMM_WORLD,2); }
    fseek(fp, 0, SEEK_END);
    long sz = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    char* json_data = malloc(sz+1); fread(json_data,1,sz,fp); fclose(fp); json_data[sz]=0;

    cJSON* root = cJSON_Parse(json_data);
    if (!root) { fprintf(stderr,"Parse error JSON\n"); MPI_Abort(MPI_COMM_WORLD,3);}
    cJSON* chunk_paths = cJSON_GetObjectItemCaseSensitive(root, "8-Chunk paths");
    if (!chunk_paths) { fprintf(stderr,"'8-Chunk paths' not found\n"); MPI_Abort(MPI_COMM_WORLD,4); }

    // Alloco liste per ogni rank
    int* cnts = calloc(size, sizeof(int));
    int* maxs = calloc(size, sizeof(int));
    RankHop** rhvec = calloc(size, sizeof(RankHop*));
    for (int i=0; i<size; ++i) rhvec[i]=NULL;

    cJSON* path_entry = NULL;
    cJSON_ArrayForEach(path_entry, chunk_paths) {
        if (!path_entry->string) continue;
        int demand, chunk, origin, met_epoch;
        parse_key(path_entry->string, &demand, &chunk, &origin, &met_epoch);
        int num_hops = cJSON_GetArraySize(path_entry);
        for (int i = 0; i < num_hops; ++i) {
            cJSON* hop_item = cJSON_GetArrayItem(path_entry, i);
            int src, dst, epoch;
            parse_hop_string(hop_item->valuestring, &src, &dst, &epoch);
            for (int t=0; t<2; ++t) {
                int who = (t==0?src:dst);
                if (t==1 && src==dst) break;
                if (cnts[who]>=maxs[who]) {
                    maxs[who] = maxs[who] ? maxs[who]*2:16;
                    rhvec[who] = realloc(rhvec[who], maxs[who]*sizeof(RankHop));
                }
                RankHop* rh = &rhvec[who][cnts[who]++];
                rh->demand = demand; rh->chunk = chunk; rh->origin=origin; rh->met_epoch=met_epoch;
                rh->hop.src=src; rh->hop.dst=dst; rh->hop.epoch=epoch;
            }
        }
    }
    // Invia
    for (int i=1; i<size; ++i) {
        int bsize;
        char* buf = serialize_ranks(rhvec[i], cnts[i], &bsize);
        MPI_Send(&bsize, 1, MPI_INT, i, 444, MPI_COMM_WORLD);
        MPI_Send(buf, bsize, MPI_BYTE, i, 445, MPI_COMM_WORLD);
        free(buf);
        free(rhvec[i]);
    }
    num_rank_hops = cnts[0];
    rank_hops = rhvec[0];
    free(cnts); free(maxs); free(rhvec);
    cJSON_Delete(root); free(json_data);
}
void receive_my_hops(int my_rank) {
    int bsize;
    MPI_Recv(&bsize, 1, MPI_INT, 0, 444, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
    char* buf = malloc(bsize);
    MPI_Recv(buf, bsize, MPI_BYTE, 0, 445, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
    rank_hops = deserialize_ranks(buf, &num_rank_hops);
    free(buf);
}

// SOLO RANK 0: parsa la config
void parse_config_if_rank0(const char* config_file, ConfigParams* conf, int my_rank) {
    if (my_rank != 0) return;
    FILE* fp = fopen(config_file, "r");
    if (!fp) { perror("Open config"); MPI_Abort(MPI_COMM_WORLD,1);}
    fseek(fp,0,SEEK_END); long sz=ftell(fp); fseek(fp,0,SEEK_SET);
    char* d = malloc(sz+1); fread(d,1,sz,fp); fclose(fp); d[sz]=0;
    cJSON* root = cJSON_Parse(d);
    if (!root) { fprintf(stderr,"Config parse error"); MPI_Abort(MPI_COMM_WORLD,1);}
    cJSON* top = cJSON_GetObjectItemCaseSensitive(root, "TopologyParams");
    cJSON* inst = cJSON_GetObjectItemCaseSensitive(root, "InstanceParams");
    conf->num_groups = cJSON_GetObjectItemCaseSensitive(top,"num_groups")->valueint;
    conf->leaf_routers = cJSON_GetObjectItemCaseSensitive(top,"leaf_routers")->valueint;
    conf->spine_routers = cJSON_GetObjectItemCaseSensitive(top,"spine_routers")->valueint;
    conf->hosts_per_router = cJSON_GetObjectItemCaseSensitive(top,"hosts_per_router")->valueint;
    conf->chunk_size = cJSON_GetObjectItemCaseSensitive(top,"chunk_size")->valueint;
    conf->num_chunks = cJSON_GetObjectItemCaseSensitive(inst,"num_chunks")->valueint;
    conf->num_epochs = cJSON_GetObjectItemCaseSensitive(inst,"num_epochs")->valueint;
    cJSON_Delete(root); free(d);
}

// Simula comunicazione di una epoca
void simulate_epoch(int rank, int size, int epoch, int chunk_int_size, int* simulated_allgather, int data_size) {
    int* recv_chunk = NULL, recv_alloc = 0;
    for (int i=0; i<num_rank_hops; ++i) {
        RankHop* rh = &rank_hops[i];
        if (rh->hop.epoch != epoch) continue;
        if (rh->hop.src == rh->hop.dst) continue;
        int* origin_chunk_ptr = simulated_allgather + rh->origin * data_size + rh->chunk * chunk_int_size;
        if (rank == rh->hop.src)
            MPI_Send(origin_chunk_ptr, chunk_int_size, MPI_INT, rh->hop.dst, epoch, MPI_COMM_WORLD);
        else if (rank == rh->hop.dst) {
            if (!recv_alloc) { recv_chunk = malloc(chunk_int_size*sizeof(int)); recv_alloc=1; }
            MPI_Recv(recv_chunk, chunk_int_size, MPI_INT, rh->hop.src, epoch, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
            memcpy(simulated_allgather + rh->origin * data_size + rh->chunk * chunk_int_size, recv_chunk, chunk_int_size*sizeof(int));
        }
    }
    if (recv_alloc) free(recv_chunk);
}
void compare_with_mpi_allgather(int rank, int size, int data_size, int* my_data, int* simulated_allgather) {
    int total = data_size*size;
    int* mpi_buf = malloc(total*sizeof(int));
    MPI_Allgather(my_data, data_size, MPI_INT, mpi_buf, data_size, MPI_INT, MPI_COMM_WORLD);
    int mismatch = 0;
    for (int i=0; i<total; i++)
        if (mpi_buf[i]!=simulated_allgather[i])
            { mismatch=1; printf("Rank %d Diff at %d: sim=%d MPI=%d\n",rank,i,simulated_allgather[i],mpi_buf[i]); }
    if (mismatch) printf("Rank %d: ERRORE DIFFERENZE!\n",rank);
    else printf("Rank %d: SUCCESSO\n",rank);
    free(mpi_buf);
}

int main(int argc, char** argv) {
    MPI_Init(&argc, &argv);
    int rank, size; MPI_Comm_rank(MPI_COMM_WORLD, &rank); MPI_Comm_size(MPI_COMM_WORLD, &size);

    if (argc != 3) {
        if (rank == 0) fprintf(stderr, "Uso: %s config.json schedule.json\n",argv[0]);
        MPI_Finalize(); return 99;
    }
    // 1. SOLO rank 0 legge la config, fa broadcast della struct a tutti
    ConfigParams conf;
    parse_config_if_rank0(argv[1], &conf, rank);
    MPI_Bcast(&conf, sizeof(ConfigParams), MPI_BYTE, 0, MPI_COMM_WORLD);

    int r = conf.leaf_routers + conf.spine_routers;
    int total_processes = conf.num_groups * r * conf.hosts_per_router;
    if (size != total_processes) {
        if (rank==0) fprintf(stderr,"Errore num processi MPI %d vs %d!\n",size,total_processes);
        MPI_Finalize(); return 1;
    }

    // 2. Rank 0 parsa solo lui ed invia solo la porzione di hop di ogni rank
    if (rank==0) distrib_parse_json_and_scatter(argv[2], size, rank);
    else         receive_my_hops(rank);

    int chunk_int_size = conf.chunk_size/sizeof(int);
    int data_size = conf.num_chunks*chunk_int_size;
    int* my_data = malloc(data_size*sizeof(int));
    srand(time(NULL)+rank*100); for (int i=0;i<data_size;i++) my_data[i]=rand();
    int total_data_size = data_size*size;
    int* simulated_allgather = malloc(total_data_size*sizeof(int));
    memset(simulated_allgather,0,total_data_size*sizeof(int));
    memcpy(simulated_allgather+rank*data_size,my_data,data_size*sizeof(int));

    int max_epoch=0;
    for (int i=0;i<num_rank_hops;i++) if (rank_hops[i].hop.epoch>max_epoch) max_epoch=rank_hops[i].hop.epoch;

    if (rank==0) printf("Inizio simulazione con %d epoche...\n", conf.num_epochs);

    MPI_Barrier(MPI_COMM_WORLD);
    double t_start = MPI_Wtime();
    for (int epoch=0; epoch<=max_epoch; ++epoch)
        simulate_epoch(rank, size, epoch, chunk_int_size, simulated_allgather, data_size);
    MPI_Barrier(MPI_COMM_WORLD);
    double t_end = MPI_Wtime();

    if (rank==0) printf("Sim avanzata e distribuita completata in %f secondi\n", t_end-t_start);
    compare_with_mpi_allgather(rank, size, data_size, my_data, simulated_allgather);

    free(my_data); free(simulated_allgather); free(rank_hops);
    MPI_Finalize();
    return 0;
}
