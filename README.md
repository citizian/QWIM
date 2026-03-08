# QWIM (Quick Worldwide Instant Messenger)

QWIM is a high-performance, real-time command-line chat application written in modern C++17. It utilizes non-blocking sockets and Linux `epoll` edge-triggered (ET) multiplexing to handle concurrent client connections efficiently without the overhead of thread-per-client architectures.

All communication between the client and server is strictly typed and serialized using a robust JSON protocol.

## ✨ Key Features

*   **High Performance Engine**: The server is driven by a single-threaded Linux `epoll` event loop with non-blocking I/O.
*   **Structured Protocol**: Messages are exchanged securely over TCP using a 4-byte network byte-order length header followed by `nlohmann::json` payloads.
*   **Slash Command UI**: Clean, IRC-style client interface driven by `/` commands.
*   **Private & Group Chat**: Send global broadcasts or route messages directly to specific users in `O(1)` time.
*   **Live Presence**: Global broadcast alerts when users dynamically log in or disconnect.
*   **Chat History Logging**: New users instantly receive a replay of the recent chat history up to a customizable limit.
*   **Smart Keep-Alive**: Multi-threaded clients run background heartbeat probes. The server sweeps and prunes "dead" or ghosted TCP connections automatically.
*   **Persistent Server Logging**: All system events, errors, and chat data are formatted with timestamps and persisted to disk.

## 📂 Project Structure

```text
QWIM/
├── CMakeLists.txt          # Root CMake build configuration
├── config/                 # Deployment config files
│   └── server.conf         
├── src/                    # Source code directory
│   ├── server.cpp          # Epoll server core
│   ├── client.cpp          # Multi-threaded client
│   └── json.hpp            # nlohmann::json dependencies
└── logs/                   # Auto-generated runtime logs
    └── server.log          
```

## 🛠️ Build Dependencies

*   **OS**: Linux (Relies on `<sys/epoll.h>`)
*   **Compiler**: C++17 compatible (GCC / Clang)
*   **Build System**: CMake 3.10+
*   **Libraries**: `pthread` (client-side background threading)

## 🚀 Building the Project

QWIM uses `CMake` for clean, out-of-source builds.

```bash
cd QWIM
mkdir build && cd build
cmake ..
make
```

Upon success, the compiled binaries `server_bin` and `client_bin` will be generated inside the `build/` directory.

## ⚙️ Configuration

The server dynamically adjusts its behavior based on `config/server.conf`. 
*(It will safely fallback to defaults if the file is missing or corrupted).*

| Key | Default | Description |
| :--- | :--- | :--- |
| `port` | 8080 | TCP Port the server binds and listens on. |
| `heartbeat_timeout` | 30 | Seconds till an unresponsive client is disconnected. |
| `history_size` | 50 | Number of recent messages to replay to new joins. |
| `logfile` | `logs/server.log` | Relative path where central logs are written. |

## 🕹️ Usage Guide

> **Note:** Because the server parses configuration and log output paths relative to its execution environment, it is highly recommended to run the binaries from the root `QWIM/` folder.

### Start the Server

Terminal 1:
```bash
# Execute from the QWIM/ root directory
./build/server_bin
```

### Start a Client

Terminal 2:
```bash
./build/client_bin
```
When prompted, authorize yourself with a unique Username to enter the chat.

### Client Slash Commands

Once connected, you can use the following commands inside the client terminal:

*   **`<text>`**: Anything not starting with a `/` is broadcast to everyone in the group chat.
*   **/pm `<user>` `<message>`**: Deliver a private message exclusively to another online user.
*   **/list**: Request a realtime array of all active connected users from the server.
*   **/help**: Display local formatting assistance.
*   **/quit**: Safely clean up network sockets and exit the application.
