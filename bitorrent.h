#include <mpi.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>
#include <iostream>
#include <random>
#include <fstream>
#include <string>

#define TRACKER_RANK 0
#define MAX_FILES 10
#define MAX_FILENAME 20
#define HASH_SIZE 33
#define MAX_CHUNKS 100
#define MAX_CLIENTS 10

using namespace std;


typedef struct {
    char filename[MAX_FILENAME];
    int totalChunks;
    int fileSize;
    char chunks[MAX_CHUNKS][HASH_SIZE];
} FileInfo;

typedef struct {
    int num_owned_files;
    vector<FileInfo> ownedFiles;
    int num_wanted_files;
    vector<FileInfo> wantedFiles;
} PeerData;



struct Client {
    int Own;
    FileInfo fileInformation;
    // Keep track of the number of active uploads
    // To prevent a peer from being overloaded
    int active_uploads;
};


enum class MpiTag : int {
    INIT            = 100,
    FILE_INFO       = 101,
    REQUEST         = 102,
    PARTIAL_DONE    = 103,
    ACTUALIZE       = 104,
    TOTAL_DONE      = 105,
    PEER_REQUEST    = 106,
    PEER_RESPONSE   = 107,
    UPLOAD_THREAD   = 108,
    UPDATE_OWNER    = 109
};

// Existing functions
PeerData read_input_file(const char *filename);

void constructMPI(MPI_Datatype *FileInfo_type);


FileInfo transferFile(const FileInfo& source) ;
void saveFile(int rank, const FileInfo &info);

void *download_thread_func(void *arg);
void *upload_thread_func(void *arg);
void addToSwarm(std::vector<std::vector<Client>> &swarm_pool,
                const FileInfo &file, int rank);
void tracker(int numtasks, int rank);
void peer(int numtasks, int rank);
void solveRequest(int rank);
void uploadFilesToTracker(MPI_Datatype FileInfo_type, int tag);
void requestDesiredFilesFromTracker(int rank, MPI_Datatype *FileInfo_type);

// Download thread helper functions
void initialize_swarm_pool(std::vector<std::vector<Client>> &swarm_pool);
void send_initialization_to_tracker(int rank, MPI_Datatype FileInfo_type);
void update_tracker_with_owned_files(MPI_Datatype FileInfo_type);
void request_file_swarms(int rank, int desired_files_count, int *downloaded, MPI_Datatype FileInfo_type);
void download_missing_chunks(int rank, int *desired_files_count, int *downloaded);

// Tracker helper functions
void send_ack_to_peers(int numtasks);
void handle_request_tag(int source, int received_data,
                        std::vector<std::vector<Client>> &swarm_pool,
                        MPI_Datatype file_info_type);
void handle_update_tag(int received_data,
                       int source,
                       std::vector<std::vector<Client>> &swarm_pool,
                       MPI_Datatype file_info_type);
void shutdown_upload_threads(int numtasks);
void initialize_swarm_pool(int desired_files_count);
void receive_initialization_data(int numtasks,
                                MPI_Datatype file_info_type,
                                std::vector<std::vector<Client>> &swarm_pool);
bool processUploadRequest(const std::string& filename, const std::string& hash, int requester_rank, MPI_Datatype file_info_type);
void sendAcknowledgment(int destination, int tag, MPI_Comm communicator);
string receiveString(int source, int tag, MPI_Comm communicator, int max_length);
Client selectPeerWithChunk(const std::string& target_filename, int required_chunk_index);

// Variables
PeerData peerInfoData;
vector<vector<Client>> clientStorage(MAX_FILES, vector<Client>(MAX_CLIENTS));