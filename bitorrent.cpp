#include "bitorrent.h"


PeerData read_input_file(const char* filePath) {
    std::ifstream clientEntryFIle(filePath);
    if (!clientEntryFIle.is_open()) {
        std::cerr << "Unable to open file: " << filePath << std::endl;
        exit(EXIT_FAILURE);
    }

    PeerData peerData;

    int ownedCount = 0;
    clientEntryFIle >> ownedCount;
    peerData.ownedFiles.resize(ownedCount);
    peerData.num_owned_files = ownedCount;

    for (auto& owned_file : peerData.ownedFiles) {
        int segments = 0;
        clientEntryFIle >> owned_file.filename >> segments;

        owned_file.totalChunks = segments;
        owned_file.fileSize  = segments;

        for (int j = 0; j < segments; ++j) {
            clientEntryFIle >> owned_file.chunks[j];
        }
    }

    int desiredCount = 0;
    clientEntryFIle >> desiredCount;
    peerData.wantedFiles.resize(desiredCount);
    peerData.num_wanted_files = desiredCount;

    for (auto& desired_file : peerData.wantedFiles) {
        clientEntryFIle >> desired_file.filename;
        desired_file.totalChunks = 0;
        desired_file.fileSize  = -1;
    }

    clientEntryFIle.close();
    return peerData;
}



void constructMPI(MPI_Datatype *file_info_type) {
    const int structBlocks = 4;

    int blockLengths[structBlocks] = {
        MAX_FILENAME,            // .filename
        1,                       // .totalChunks
        1,                       // .fileSize
        MAX_CHUNKS * HASH_SIZE   // .chunks
    };

    MPI_Datatype types[structBlocks] = {
        MPI_CHAR,  // for filename
        MPI_INT,   // for totalChunks
        MPI_INT,   // for fileSize
        MPI_CHAR   // for chunks
    };

    MPI_Aint offsets[structBlocks];

    FileInfo tempStruct;
    MPI_Aint baseAddress;
    MPI_Get_address(&tempStruct, &baseAddress);

    MPI_Aint addr_filename;
    MPI_Get_address(&tempStruct.filename, &addr_filename);
    offsets[0] = addr_filename - baseAddress;

    MPI_Aint addr_numChunks;
    MPI_Get_address(&tempStruct.totalChunks, &addr_numChunks);
    offsets[1] = addr_numChunks - baseAddress;

    MPI_Aint addr_fileSize;
    MPI_Get_address(&tempStruct.fileSize, &addr_fileSize);
    offsets[2] = addr_fileSize - baseAddress;

    MPI_Aint addr_chunks;
    MPI_Get_address(&tempStruct.chunks, &addr_chunks);
    offsets[3] = addr_chunks - baseAddress;

    MPI_Type_create_struct(structBlocks, blockLengths, offsets, types,
                           file_info_type);
    MPI_Type_commit(file_info_type);
}

FileInfo transferFile(const FileInfo& source) {
    FileInfo dest;

    copy(source.filename, source.filename + MAX_FILENAME, dest.filename);

    dest.totalChunks = source.totalChunks;
    dest.fileSize = source.fileSize;

    for (int i = 0; i < source.totalChunks && i < MAX_CHUNKS; ++i) {
        copy(source.chunks[i], source.chunks[i] + HASH_SIZE, dest.chunks[i]);
    }

    return dest;
}


void saveFile(int rank, const FileInfo& peerInfoData) {
    string filename = "client" + std::to_string(rank) + "_" + peerInfoData.filename;

    ofstream file(filename, std::ios::app);
    
    if (!file) {
        std::cerr << "Error opening file: " << filename << std::endl;
        return;
    }

    for (int i = 0; i < peerInfoData.totalChunks; ++i) {
        file << peerInfoData.chunks[i];
        if (i != peerInfoData.totalChunks - 1) {
            file << "\n"; // Add newline between chunks, except after the last one
        }
    }
}

void initialize_swarm_pool(int desired_files_count) {
    clientStorage.resize(MAX_FILES);
    for (auto &client : clientStorage) {
        client.resize(MAX_CLIENTS);
    }
}

void send_initialization_to_tracker(int rank, MPI_Datatype file_info_type) {
    int num_owned_files = peerInfoData.num_owned_files;
    MPI_Send(&num_owned_files, 1, MPI_INT, TRACKER_RANK, (int)MpiTag::INIT, MPI_COMM_WORLD);

    for (int i = 0; i < peerInfoData.num_owned_files; i++) {
        MPI_Send(&peerInfoData.ownedFiles[i], 1, file_info_type, TRACKER_RANK, (int)MpiTag::FILE_INFO, MPI_COMM_WORLD);
    }
}

void update_tracker_with_owned_files(MPI_Datatype file_info_type) {
    int num_owned_files = peerInfoData.num_owned_files;
    MPI_Send(&num_owned_files, 1, MPI_INT, TRACKER_RANK, (int)MpiTag::UPDATE_OWNER, MPI_COMM_WORLD);

    for (int i = 0; i < num_owned_files; i++) {
        MPI_Send(&peerInfoData.ownedFiles[i], 1, file_info_type, TRACKER_RANK, (int)MpiTag::UPDATE_OWNER, MPI_COMM_WORLD);
    }
}

void request_file_swarms(int rank,
                         int desired_files_count,
                         int *downloaded,
                         MPI_Datatype file_info_type)
{

    MPI_Send(&desired_files_count, 1, MPI_INT,
             TRACKER_RANK, (int)MpiTag::REQUEST, MPI_COMM_WORLD);

    for (int i = 0; i < peerInfoData.num_wanted_files; i++) {
        if (downloaded[i] == 1) {
            continue;
        }

        string filename(peerInfoData.wantedFiles[i].filename);
        MPI_Send(filename.c_str(), MAX_FILENAME, MPI_CHAR,
                 TRACKER_RANK, (int)MpiTag::REQUEST, MPI_COMM_WORLD);

        int number_of_files = 0;
        MPI_Recv(&number_of_files, 1, MPI_INT,
                 TRACKER_RANK, (int)MpiTag::REQUEST,
                 MPI_COMM_WORLD, MPI_STATUS_IGNORE);

        for (int k = 0; k < number_of_files; k++) {
            int fileSize = 0;
            Client recv_file;
            MPI_Recv(&fileSize, 1, MPI_INT,
                     TRACKER_RANK, (int)MpiTag::REQUEST,
                     MPI_COMM_WORLD, MPI_STATUS_IGNORE);

            MPI_Recv(&recv_file.Own, 1, MPI_INT,
                     TRACKER_RANK, (int)MpiTag::REQUEST,
                     MPI_COMM_WORLD, MPI_STATUS_IGNORE);

            MPI_Recv(&recv_file.fileInformation, 1, file_info_type,
                     TRACKER_RANK, (int)MpiTag::REQUEST,
                     MPI_COMM_WORLD, MPI_STATUS_IGNORE);

            peerInfoData.wantedFiles[i].fileSize = fileSize;

            bool inserted = false;
            for (int j = 0; j < peerInfoData.num_wanted_files && !inserted; j++) {

                if (strcmp(clientStorage[j][0].fileInformation.filename, "") == 0) {
                    clientStorage[j][0].fileInformation = transferFile(recv_file.fileInformation);
                    clientStorage[j][0].Own = recv_file.Own;
                    inserted = true;
                }

                else if (strcmp(clientStorage[j][0].fileInformation.filename,
                                filename.c_str()) == 0)
                {
                    for (int c = 0; c < MAX_CLIENTS; c++) {
                        if (clientStorage[j][c].Own == 0) {
                            clientStorage[j][c].fileInformation = recv_file.fileInformation;
                            clientStorage[j][c].Own = recv_file.Own;
                            inserted = true;
                            break;
                        }
                        if (recv_file.Own == clientStorage[j][c].Own) {
                            clientStorage[j][c].fileInformation = recv_file.fileInformation;
                            inserted = true;
                            break;
                        }
                    }
                }
            }
        }
    }
}


Client selectPeerWithChunk(const std::string& target_filename, int required_chunk_index) {
    Client selected_peer = {0, FileInfo(), 0};
    int min_load = INT32_MAX;

    for (const auto& swarm : clientStorage) {
        if (swarm.empty()) continue;

        if (std::string(swarm[0].fileInformation.filename) != target_filename) {
            continue;
        }

        for (const auto& client : swarm) {
            if (std::string(client.fileInformation.filename).empty()) {
                continue;
            }
            if (client.fileInformation.totalChunks > required_chunk_index) {
                if (client.active_uploads < min_load) {
                    min_load = client.active_uploads;
                    selected_peer = client;
                }
            }
        }
    }

    return selected_peer;
}


void download_missing_chunks(int rank, int* desired_files_count, int* downloaded) {
    for (int i = 0; i < peerInfoData.num_wanted_files; i++) {
        if (downloaded[i] == 1) {
            continue;
        }

        int desired_file_index = i;
        int owned_file_index = -1;
        string filename(peerInfoData.wantedFiles[desired_file_index].filename);
        int chunk_index = -1;

        // Find the owned file
        for (size_t j = 0; j < peerInfoData.ownedFiles.size(); j++) {
            if (string(peerInfoData.ownedFiles[j].filename) == filename) {
                chunk_index = peerInfoData.ownedFiles[j].totalChunks;
                owned_file_index = j;
                break;
            }
        }

        // If not found, add the desired file to ownedFiles
        if (owned_file_index == -1) {
            FileInfo newFile = transferFile(peerInfoData.wantedFiles[desired_file_index]);
            peerInfoData.ownedFiles.push_back(newFile);
            owned_file_index = static_cast<int>(peerInfoData.ownedFiles.size()) - 1;
            chunk_index = 0;
        }

        // Select a peer that has the required chunk
        Client peer = selectPeerWithChunk(filename, chunk_index);
        if (peer.Own == 0) {
            cerr << "No peer found with the required chunk for file: " << filename << endl;
            continue; 
        }

        // Send requests to the selected peer
        MPI_Send(&rank, 1, MPI_INT, peer.Own, static_cast<int>(MpiTag::UPLOAD_THREAD), MPI_COMM_WORLD);
        MPI_Send(peerInfoData.wantedFiles[desired_file_index].filename, MAX_FILENAME, MPI_CHAR, peer.Own, static_cast<int>(MpiTag::PEER_REQUEST), MPI_COMM_WORLD);
        MPI_Send(peer.fileInformation.chunks[chunk_index], HASH_SIZE, MPI_CHAR, peer.Own, static_cast<int>(MpiTag::PEER_REQUEST), MPI_COMM_WORLD);

        // Receive acknowledgment
        int ack;
        MPI_Recv(&ack, 1, MPI_INT, peer.Own, static_cast<int>(MpiTag::PEER_RESPONSE), MPI_COMM_WORLD, MPI_STATUS_IGNORE);

        if (ack == 1) {
            peerInfoData.ownedFiles[owned_file_index].totalChunks++;
            strcpy(peerInfoData.ownedFiles[owned_file_index].chunks[chunk_index], peer.fileInformation.chunks[chunk_index]);

            if (peerInfoData.ownedFiles[owned_file_index].totalChunks == peerInfoData.ownedFiles[owned_file_index].fileSize) {
                downloaded[i] = 1;
                (*desired_files_count)--;

                MPI_Send(&ack, 1, MPI_INT, TRACKER_RANK, static_cast<int>(MpiTag::PARTIAL_DONE), MPI_COMM_WORLD);
                saveFile(rank, peerInfoData.ownedFiles[owned_file_index]);
            }
        } else {
            cerr << "Failed to receive acknowledgment from peer " << peer.Own << " for file: " << filename << std::endl;
        }
    }
}

void *download_thread_func(void *arg) {
    int rank = *(int*)arg;
    int ack;
    int steps = 10;

    int downloaded[MAX_FILES] = { 0 };
    char filename[MAX_FILENAME];
    sprintf(filename, "in%d.txt", rank);
    peerInfoData = read_input_file(filename);
    int desired_files_count = peerInfoData.num_wanted_files;

    MPI_Datatype file_info_type;
    constructMPI(&file_info_type);

    send_initialization_to_tracker(rank, file_info_type);
    MPI_Recv(&ack, 1, MPI_INT, TRACKER_RANK, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
    initialize_swarm_pool(desired_files_count);

    while (desired_files_count > 0) {
        if (steps >= 10) {
            steps = 0;
            request_file_swarms(rank, desired_files_count, downloaded, file_info_type);
            update_tracker_with_owned_files(file_info_type);
        }

        download_missing_chunks(rank, &desired_files_count, downloaded);
        steps++;
    }

    MPI_Send(&ack, 1, MPI_INT, TRACKER_RANK, (int)MpiTag::TOTAL_DONE, MPI_COMM_WORLD);
    return NULL;
}


string receiveString(int source, int tag, MPI_Comm communicator, int max_length) {
    char buffer[max_length];
    MPI_Status status;
    MPI_Recv(buffer, max_length, MPI_CHAR, source, tag, communicator, &status);
    return std::string(buffer);
}

void sendAcknowledgment(int destination, int tag, MPI_Comm communicator) {
    int ack = 1;
    MPI_Send(&ack, 1, MPI_INT, destination, tag, communicator);
}

bool processUploadRequest(const std::string& filename, const std::string& hash, int requester_rank, MPI_Datatype file_info_type) {
    for (const auto& file : peerInfoData.ownedFiles) {
        if (strcmp(file.filename, "") == 0) {
            continue;
        }

        if (strcmp(file.filename, filename.c_str()) == 0) {
            for (const auto& chunk : file.chunks) {
                if (strncmp(hash.c_str(), chunk, HASH_SIZE) == 0) {
                    sendAcknowledgment(requester_rank, static_cast<int>(MpiTag::PEER_RESPONSE), MPI_COMM_WORLD);
                    return true;
                }
            }
            break;
        }
    }
    return false;
}

void* upload_thread_func(void* arg) {
    int rank = *static_cast<int*>(arg);
    bool continue_upload = true;

    while (continue_upload) {
        int received_data;
        MPI_Status status;

        MPI_Recv(&received_data, 1, MPI_INT, MPI_ANY_SOURCE, static_cast<int>(MpiTag::UPLOAD_THREAD), MPI_COMM_WORLD, &status);

        if (status.MPI_SOURCE == TRACKER_RANK) {
            cout << "Rank " << rank << ": Shutdown signal received. Terminating upload thread.\n";
            break;
        }

        string filename = receiveString(status.MPI_SOURCE, static_cast<int>(MpiTag::PEER_REQUEST), MPI_COMM_WORLD, MAX_FILENAME);
        string hash = receiveString(status.MPI_SOURCE, static_cast<int>(MpiTag::PEER_REQUEST), MPI_COMM_WORLD, HASH_SIZE);

        bool chunk_found = processUploadRequest(filename, hash, status.MPI_SOURCE, /*file_info_type*/ MPI_DATATYPE_NULL); // Replace with actual MPI_Datatype

        if (!chunk_found) {
            cerr << "Rank " << rank << ": Requested chunk not found for file '" << filename << "'.\n";
        }
    }

    return nullptr;
}

void addToSwarm(std::vector<std::vector<Client>> &swarm_pool,
                const FileInfo &file, int rank)
{
    for (auto &swarm : swarm_pool) {
        if (strcmp(swarm[0].fileInformation.filename, file.filename) == 0) {
            for (auto &client : swarm) {
                if (client.Own == 0 || client.Own == rank) {
                    client.Own = rank;
                    client.fileInformation = file;
                    break;
                }
            }
            break;
        }
    }

    for (auto &swarm : swarm_pool) {
        if (strcmp(swarm[0].fileInformation.filename, "") == 0) {
            swarm[0].Own = rank;
            swarm[0].fileInformation = file;
            break;
        }
    }
}


void initialize_swarm_pool(std::vector<std::vector<Client>> &swarm_pool) {
    swarm_pool.resize(MAX_FILES);
    for (auto &client : swarm_pool) {
        client.resize(MAX_CLIENTS);
    }
}

void receive_initialization_data(int numtasks,
                                MPI_Datatype file_info_type,
                                std::vector<std::vector<Client>> &swarm_pool)
{
    for (int i = 1; i < numtasks; i++) {
        int num_owned_files;
        MPI_Recv(&num_owned_files, 1, MPI_INT,
                 i, (int)MpiTag::INIT, MPI_COMM_WORLD,
                 MPI_STATUS_IGNORE);

        for (int j = 0; j < num_owned_files; j++) {
            FileInfo peerInfoData;
            MPI_Recv(&peerInfoData, 1, file_info_type, i, (int)MpiTag::FILE_INFO,
                     MPI_COMM_WORLD, MPI_STATUS_IGNORE);
            addToSwarm(swarm_pool, peerInfoData, i);
        }
    }
}


void send_ack_to_peers(int numtasks) {
    int ack = 1;
    for (int i = 1; i < numtasks; i++) {
        MPI_Send(&ack, 1, MPI_INT, i, 0, MPI_COMM_WORLD);
    }
}

void handle_request_tag(int source,
                        int received_data,
                        std::vector<std::vector<Client>> &swarm_pool,
                        MPI_Datatype file_info_type)
{
    for (int awaiting_files = 0; awaiting_files < received_data; awaiting_files++) {
        char filename[MAX_FILENAME];
        MPI_Recv(filename, MAX_FILENAME, MPI_CHAR, source,
                 (int)MpiTag::REQUEST, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

        for (int i = 0; i < MAX_FILES; i++) {
            if (strcmp(swarm_pool[i][0].fileInformation.filename, filename) == 0) {
                int number_of_owners = 0;
                while (number_of_owners < MAX_CLIENTS &&
                       strcmp(swarm_pool[i][number_of_owners].fileInformation.filename, "") != 0)
                {
                    number_of_owners++;
                }
                MPI_Send(&number_of_owners, 1, MPI_INT, source,
                         (int)MpiTag::REQUEST, MPI_COMM_WORLD);

                for (int j = 0; j < number_of_owners; j++) {
                    int fileSize = swarm_pool[i][j].fileInformation.fileSize;
                    MPI_Send(&fileSize, 1, MPI_INT, source,
                             (int)MpiTag::REQUEST, MPI_COMM_WORLD);
                    MPI_Send(&swarm_pool[i][j].Own, 1, MPI_INT, source,
                             (int)MpiTag::REQUEST, MPI_COMM_WORLD);

                    MPI_Send(&swarm_pool[i][j].fileInformation, 1, file_info_type,
                             source, (int)MpiTag::REQUEST, MPI_COMM_WORLD);
                }
                break;
            }
        }
    }
}


void handle_update_tag(int received_data,
                       int source,
                       std::vector<std::vector<Client>> &swarm_pool,
                       MPI_Datatype file_info_type)
{
    for (int j = 0; j < received_data; j++) {
        FileInfo peerInfoData;
        MPI_Recv(&peerInfoData, 1, file_info_type, source,
                 (int)MpiTag::UPDATE_OWNER, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
        addToSwarm(swarm_pool, peerInfoData, source);
    }
}

void shutdown_upload_threads(int numtasks) {
    int ack = 1;
    for (int i = 1; i < numtasks; i++) {
        MPI_Send(&ack, 1, MPI_INT, i, (int)MpiTag::UPLOAD_THREAD, MPI_COMM_WORLD);
    }
}
void tracker(int numtasks, int rank) {
    std::vector<std::vector<Client>> swarm_pool;
    initialize_swarm_pool(swarm_pool);

    MPI_Datatype file_info_type;
    constructMPI(&file_info_type);

    receive_initialization_data(numtasks, file_info_type, swarm_pool);
    send_ack_to_peers(numtasks);

    int remaining_clients = numtasks - 1;
    while (remaining_clients > 0) {
        int received_data;
        MPI_Status status;
        MPI_Recv(&received_data, 1, MPI_INT,
                 MPI_ANY_SOURCE, MPI_ANY_TAG,
                 MPI_COMM_WORLD, &status);

        int source = status.MPI_SOURCE;
        int tag    = status.MPI_TAG;

        if (tag == (int)MpiTag::REQUEST) {
            handle_request_tag(source, received_data, swarm_pool, file_info_type);
        } else if (tag == (int)MpiTag::UPDATE_OWNER) {
            handle_update_tag(received_data, source, swarm_pool, file_info_type);
        } else if (tag == (int)MpiTag::PARTIAL_DONE) {

        } else if (tag == (int)MpiTag::TOTAL_DONE) {
            remaining_clients--;
        }
    }

    shutdown_upload_threads(numtasks);
}


void peer(int numtasks, int rank) {
    pthread_t download_thread;
    pthread_t upload_thread;
    void *status;
    int r;

    r = pthread_create(&download_thread, NULL, download_thread_func, (void *) &rank);
    if (r) {
        exit(-1);
    }

    r = pthread_create(&upload_thread, NULL, upload_thread_func, (void *) &rank);
    if (r) {
        exit(-1);
    }

    r = pthread_join(download_thread, &status);
    if (r) {
        exit(-1);
    }

    r = pthread_join(upload_thread, &status);
    if (r) {
        exit(-1);
    }
}
 
int main (int argc, char *argv[]) {
    int numtasks, rank;
 
    int provided;
    MPI_Init_thread(&argc, &argv, MPI_THREAD_MULTIPLE, &provided);
    if (provided < MPI_THREAD_MULTIPLE) {
        fprintf(stderr, "MPI nu are suport pentru multi-threading\n");
        exit(-1);
    }
    MPI_Comm_size(MPI_COMM_WORLD, &numtasks);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);

    if (rank == TRACKER_RANK) {
        tracker(numtasks, rank);
    } else {
        peer(numtasks, rank);
    }

    MPI_Finalize();
}