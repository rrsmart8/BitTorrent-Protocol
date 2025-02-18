# BitTorrent Protocol Simulation

<img src="/bittorrent-overview.png" alt="BitTorrent Diagram" width="600"/>

## Description
This project simulates a simplified **BitTorrent** protocol using **MPI** for distributed communication and **Pthreads** for concurrency. Multiple **peers** share file segments, coordinate via a **tracker**, and download missing segments from each other in parallel.

## Implementation Details

### 1. Roles and MPI Tasks
- **Tracker (rank 0)**: Keeps track of which peers own or partially own each file (the “swarm”).  
- **Peers (rank > 0)**: Can own files fully (as seeds), partially (peers), or not at all (leechers) at the start. They request missing segments from other peers and respond to segment requests they can fulfill.

### 2. File and Segment Management
- Each file is split into **segments**, identified by 32-character hashes.
- A peer announces the segments it owns to the tracker, then receives a list of other peers that have the missing segments it needs.

### 3. Thread Synchronization and Work Distribution
Each peer spawns two threads:
1. **Download Thread**  
   - Communicates with the tracker to get swarm information.  
   - Requests needed file segments from peers.  
   - Updates the tracker once a file is fully downloaded.

2. **Upload Thread**  
   - Listens for incoming requests from peers.  
   - Checks if it owns the requested segment and replies with an acknowledgment if available.

## Parallel Execution

### **1. Tracker Behavior**
- Waits for initialization messages from all peers about the files they own.
- Maintains a swarm list for each file (i.e., which peers own it partially or fully).
- Receives requests for swarm updates and provides a list of peers for the requested file.

### **2. Peer Lifecycle**
1. **Initialization**  
   - Reads local input (e.g., `in1.txt`), listing files owned and wanted.  
   - Registers owned files with the tracker.
2. **Downloading**  
   - Fetches swarm info from the tracker.  
   - Requests missing segments from peers that have them.  
   - Saves the complete file locally once all segments are received.
3. **Uploading**  
   - Responds to segment requests from other peers throughout runtime.
4. **Completion**  
   - Notifies the tracker after downloading all desired files.  
   - Continues to seed (upload) until the tracker indicates all peers are done.

## Build and Run

1. **Compile**  
   ```bash
   make
   ```
   Generates the `bitorrent` executable.

2. **Provide Input Files**  
   - For each peer (rank > 0), create `in<R>.txt` containing:
     - Number of files owned + their hashes
     - Number of files wanted

3. **Execute**  
   ```bash
   mpirun -np <NUM_TASKS> ./bitorrent
   ```
   - Rank `0` is the tracker.
   - Ranks `1..N-1` are peers.

## Example Usage
Assume **4** MPI tasks: `rank 0` (tracker) and `rank 1,2,3` (peers).  
Each peer file (`in1.txt`, `in2.txt`, `in3.txt`) details the owned and desired files.  
Run:  
```bash
mpirun -np 4 ./bitorrent
```
- The tracker collects each peer’s initial file data.
- Peers periodically request swarm updates and download missing segments.
- Once complete, each peer outputs downloaded files as `client<R>_<filename>`.

## Notes
- Uses **MPI** for inter-process communication and **Pthreads** for concurrency within each peer.
- Maintains proper synchronization with **mutexes** and **barriers** where necessary.
- Provides a simple load distribution by letting peers pick relatively less busy peers to request segments from.
