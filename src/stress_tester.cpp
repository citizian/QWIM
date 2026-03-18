#include <iostream>
#include <vector>
#include <thread>
#include <atomic>
#include <chrono>
#include <string>
#include <cstring>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <sys/resource.h>
#include <fcntl.h>
#include <fstream>
#include <sstream>
#include <mutex>
#include <algorithm>
#include <numeric>
#include <iomanip>
#include <csignal>

// ============ Global Metrics ============
std::atomic<long long> g_messages_received(0);
std::atomic<long long> g_messages_sent(0);
std::atomic<int> g_active_connections(0);
std::atomic<int> g_failed_connections(0);
std::atomic<int> g_connect_errors(0);
std::atomic<int> g_login_sent(0);
std::atomic<int> g_heartbeat_sent(0);
std::atomic<int> g_disconnected(0);
std::atomic<bool> g_running(true);

// Per-second snapshot data for stability analysis
struct Snapshot {
    int active_conns;
    int failed_conns;
    long long tx_qps;
    long long rx_qps;
    double cpu_usage;
};
std::mutex g_snapshot_mutex;
std::vector<Snapshot> g_snapshots;

// ============ Utility Functions ============
int create_nonblocking_socket() {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd == -1) return -1;
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    return fd;
}

bool wait_for_connect(int fd, int timeout_ms) {
    fd_set wset;
    FD_ZERO(&wset);
    FD_SET(fd, &wset);
    struct timeval tv;
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    int ret = select(fd + 1, nullptr, &wset, nullptr, &tv);
    if (ret > 0) {
        int so_error = 0;
        socklen_t len = sizeof(so_error);
        getsockopt(fd, SOL_SOCKET, SO_ERROR, &so_error, &len);
        return so_error == 0;
    }
    return false;
}

void send_json_packet(int fd, const std::string& json_str) {
    uint32_t len = htonl(json_str.length());
    std::vector<char> packet(4 + json_str.length());
    memcpy(packet.data(), &len, 4);
    memcpy(packet.data() + 4, json_str.c_str(), json_str.length());
    send(fd, packet.data(), packet.size(), MSG_NOSIGNAL);
}

// ============ CPU Usage Tracker ============
class CpuTracker {
public:
    explicit CpuTracker(pid_t pid) : pid_(pid), last_total_time_(0) {
        last_time_ = std::chrono::steady_clock::now();
        // Prime the first reading
        readCpu();
    }

    double readCpu() {
        if (pid_ <= 0) return 0.0;

        std::string stat_file = "/proc/" + std::to_string(pid_) + "/stat";
        std::ifstream ifs(stat_file);
        if (!ifs.is_open()) return 0.0;

        std::string line;
        std::getline(ifs, line);
        std::istringstream iss(line);
        std::string token;

        long utime = 0, stime = 0;
        for (int i = 1; i <= 14; ++i) {
            iss >> token;
            if (i == 14) utime = std::stol(token);
        }
        iss >> token; stime = std::stol(token);

        long total_time = utime + stime;
        auto now = std::chrono::steady_clock::now();
        double dt = std::chrono::duration<double>(now - last_time_).count();

        double usage = 0.0;
        if (last_total_time_ != 0 && dt > 0) {
            usage = 100.0 * (total_time - last_total_time_) / sysconf(_SC_CLK_TCK) / dt;
        }

        last_total_time_ = total_time;
        last_time_ = now;
        return usage;
    }

private:
    pid_t pid_;
    long last_total_time_;
    std::chrono::steady_clock::time_point last_time_;
};

// ============ Worker Thread ============
void worker_thread(int thread_id, const std::string& ip, int port,
                   int num_connections, int messages_per_sec,
                   bool do_login, bool do_heartbeat) {
    int epoll_fd = epoll_create1(0);
    if (epoll_fd == -1) return;

    struct sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    inet_pton(AF_INET, ip.c_str(), &server_addr.sin_addr);

    std::vector<int> fds;

    // ---- Connect phase ----
    for (int i = 0; i < num_connections && g_running; ++i) {
        int fd = create_nonblocking_socket();
        if (fd == -1) {
            g_failed_connections++;
            g_connect_errors++;
            continue;
        }

        int ret = connect(fd, (struct sockaddr*)&server_addr, sizeof(server_addr));
        if (ret == -1 && errno != EINPROGRESS) {
            close(fd);
            g_failed_connections++;
            g_connect_errors++;
            continue;
        }

        // Wait for connection to complete (up to 3s)
        if (!wait_for_connect(fd, 3000)) {
            close(fd);
            g_failed_connections++;
            g_connect_errors++;
            continue;
        }

        struct epoll_event ev;
        ev.events = EPOLLIN | EPOLLOUT | EPOLLET;
        ev.data.fd = fd;
        if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &ev) == 0) {
            fds.push_back(fd);
            g_active_connections++;

            // Send login message immediately after connecting
            if (do_login) {
                std::string user = "stress_user_" + std::to_string(thread_id) + "_" + std::to_string(i);
                std::string login_json = R"({"type":"login","user":")" + user + R"("})";
                send_json_packet(fd, login_json);
                g_login_sent++;
            }
        } else {
            close(fd);
            g_failed_connections++;
        }

        // Small delay between connections to avoid SYN flood
        if (i % 50 == 49) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }

    // ---- Prepare chat message ----
    std::string chat_payload = R"({"type":"chat","msg":"stress_test_message"})";

    auto last_send_time = std::chrono::steady_clock::now();
    auto last_heartbeat_time = std::chrono::steady_clock::now();
    double send_interval = messages_per_sec > 0 ? (1.0 / messages_per_sec) : 1.0;

    struct epoll_event events[1024];

    // ---- Event loop ----
    while (g_running) {
        int n = epoll_wait(epoll_fd, events, 1024, 10);
        for (int i = 0; i < n; ++i) {
            int efd = events[i].data.fd;

            if (events[i].events & (EPOLLHUP | EPOLLERR)) {
                epoll_ctl(epoll_fd, EPOLL_CTL_DEL, efd, nullptr);
                close(efd);
                fds.erase(std::remove(fds.begin(), fds.end(), efd), fds.end());
                g_active_connections--;
                g_disconnected++;
                continue;
            }

            if (events[i].events & EPOLLIN) {
                char buf[4096];
                while (true) {
                    ssize_t bytes = read(efd, buf, sizeof(buf));
                    if (bytes > 0) {
                        g_messages_received++;
                    } else if (bytes == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                        break;
                    } else { // bytes == 0 (peer disconnect) or error
                        epoll_ctl(epoll_fd, EPOLL_CTL_DEL, efd, nullptr);
                        close(efd);
                        fds.erase(std::remove(fds.begin(), fds.end(), efd), fds.end());
                        g_active_connections--;
                        g_disconnected++;
                        break;
                    }
                }
            }
        }

        auto now = std::chrono::steady_clock::now();

        // Send chat messages at specified rate
        if (messages_per_sec > 0 && std::chrono::duration<double>(now - last_send_time).count() >= send_interval) {
            last_send_time = now;
            for (int fd : fds) {
                if (send(fd, nullptr, 0, MSG_NOSIGNAL) == -1 && errno == EPIPE) continue; // check alive
                send_json_packet(fd, chat_payload);
                g_messages_sent++;
            }
        }

        // Send heartbeat every 10 seconds to keep connections alive
        if (do_heartbeat && std::chrono::duration<double>(now - last_heartbeat_time).count() >= 10.0) {
            last_heartbeat_time = now;
            std::string hb = R"({"type":"heartbeat"})";
            for (int fd : fds) {
                send_json_packet(fd, hb);
                g_heartbeat_sent++;
            }
        }
    }

    for (int fd : fds) {
        close(fd);
    }
    close(epoll_fd);
}

// ============ Print Helpers ============
const char* RESET  = "\033[0m";
const char* BOLD   = "\033[1m";
const char* RED    = "\033[31m";
const char* GREEN  = "\033[32m";
const char* YELLOW = "\033[33m";
const char* CYAN   = "\033[36m";
const char* BLUE   = "\033[34m";

void print_header(const std::string& title) {
    std::cout << "\n" << BOLD << CYAN
              << "╔══════════════════════════════════════════════════════════════╗\n"
              << "║  " << std::left << std::setw(60) << title << "║\n"
              << "╚══════════════════════════════════════════════════════════════╝"
              << RESET << "\n\n";
}

void print_separator() {
    std::cout << CYAN << "────────────────────────────────────────────────────────────────" << RESET << "\n";
}

// ============ Stability Analysis ============
struct TestResult {
    std::string test_name;
    bool passed;
    std::string detail;
};

std::vector<TestResult> analyze_stability(const std::vector<Snapshot>& snaps) {
    std::vector<TestResult> results;

    if (snaps.empty()) {
        results.push_back({"Stability Analysis", false, "No data collected"});
        return results;
    }

    // 1. Connection stability: check if connections stayed alive
    int max_conns = 0, min_conns = snaps[0].active_conns;
    for (const auto& s : snaps) {
        max_conns = std::max(max_conns, s.active_conns);
        min_conns = std::min(min_conns, s.active_conns);
    }
    double conn_drop_rate = max_conns > 0 ? 100.0 * (max_conns - snaps.back().active_conns) / max_conns : 0.0;
    results.push_back({
        "Connection Stability",
        conn_drop_rate < 10.0,
        "Max: " + std::to_string(max_conns) + ", End: " + std::to_string(snaps.back().active_conns) +
        ", Drop Rate: " + std::to_string((int)conn_drop_rate) + "%"
    });

    // 2. QPS stability: check coefficient of variation of TX QPS (skip first 5s warmup)
    std::vector<double> tx_values;
    for (size_t i = 5; i < snaps.size(); ++i) {
        tx_values.push_back(snaps[i].tx_qps);
    }
    if (!tx_values.empty()) {
        double mean = std::accumulate(tx_values.begin(), tx_values.end(), 0.0) / tx_values.size();
        double sq_sum = 0;
        for (double v : tx_values) sq_sum += (v - mean) * (v - mean);
        double stddev = std::sqrt(sq_sum / tx_values.size());
        double cv = mean > 0 ? stddev / mean : 0;

        results.push_back({
            "QPS Stability (CV)",
            cv < 0.3,
            "Mean TX: " + std::to_string((long long)mean) + " msg/s, StdDev: " + std::to_string((long long)stddev) +
            ", CV: " + std::to_string(cv).substr(0, 5)
        });
    }

    // 3. CPU stability: check if CPU usage remained within bounds
    double max_cpu = 0, avg_cpu = 0;
    int cpu_count = 0;
    for (const auto& s : snaps) {
        if (s.cpu_usage > 0) {
            max_cpu = std::max(max_cpu, s.cpu_usage);
            avg_cpu += s.cpu_usage;
            cpu_count++;
        }
    }
    if (cpu_count > 0) {
        avg_cpu /= cpu_count;
        results.push_back({
            "CPU Usage",
            max_cpu < 90.0,
            "Avg: " + std::to_string(avg_cpu).substr(0, 6) + "%, Peak: " + std::to_string(max_cpu).substr(0, 6) + "%"
        });
    }

    // 4. Server crash check: did all connections drop to 0 unexpectedly?
    bool server_crashed = false;
    for (size_t i = 5; i < snaps.size(); ++i) {
        if (snaps[i].active_conns == 0 && max_conns > 10) {
            server_crashed = true;
            break;
        }
    }
    results.push_back({
        "Server Alive",
        !server_crashed,
        server_crashed ? "All connections dropped - possible server crash!" : "Server stayed responsive"
    });

    return results;
}

// ============ Signal Handler ============
void signal_handler(int sig) {
    g_running = false;
}

// ============ Main ============
int main(int argc, char* argv[]) {
    signal(SIGPIPE, SIG_IGN);
    signal(SIGINT, signal_handler);

    if (argc < 6) {
        std::cerr << "Usage: " << argv[0]
                  << " <ip> <port> <connections_per_thread> <num_threads> <msgs_per_conn_per_sec> [server_pid] [duration_sec]\n"
                  << "\nExample: " << argv[0] << " 127.0.0.1 8080 100 4 10 $(pgrep server_bin) 60\n";
        return 1;
    }

    std::string ip = argv[1];
    int port = std::stoi(argv[2]);
    int conns_per_thread = std::stoi(argv[3]);
    int num_threads = std::stoi(argv[4]);
    int msgs_per_sec = std::stoi(argv[5]);
    pid_t target_pid = (argc > 6) ? std::stoi(argv[6]) : 0;
    int duration = (argc > 7) ? std::stoi(argv[7]) : 60;

    int total_conns = num_threads * conns_per_thread;

    // Check system limits
    struct rlimit lim;
    getrlimit(RLIMIT_NOFILE, &lim);

    print_header("QWIM Server Stress Test");

    std::cout << BOLD << "Test Configuration:" << RESET << "\n";
    std::cout << "  Target:             " << ip << ":" << port << "\n";
    std::cout << "  Threads:            " << num_threads << "\n";
    std::cout << "  Conns/Thread:       " << conns_per_thread << "\n";
    std::cout << "  Total Connections:  " << total_conns << "\n";
    std::cout << "  Msgs/Conn/Sec:      " << msgs_per_sec << "\n";
    std::cout << "  Duration:           " << duration << "s\n";
    std::cout << "  Server PID:         " << (target_pid > 0 ? std::to_string(target_pid) : "N/A") << "\n";
    std::cout << "  FD Limit (soft):    " << lim.rlim_cur << "\n";
    std::cout << "  FD Limit (hard):    " << lim.rlim_max << "\n";

    if ((unsigned long)total_conns > lim.rlim_cur - 50) {
        std::cout << YELLOW << "\n  ⚠ Warning: requested connections (" << total_conns
                  << ") is close to FD limit (" << lim.rlim_cur << ")" << RESET << "\n";
        std::cout << "  Consider running: ulimit -n " << (total_conns + 1000) << "\n";
    }

    print_separator();
    std::cout << BOLD << "\nPhase 1: Establishing Connections...\n" << RESET;

    CpuTracker cpu_tracker(target_pid);
    // Prime the CPU reading
    cpu_tracker.readCpu();

    std::vector<std::thread> threads;
    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back(worker_thread, i, ip, port, conns_per_thread, msgs_per_sec,
                             true /* login */, true /* heartbeat */);
        std::this_thread::sleep_for(std::chrono::milliseconds(200)); // Stagger to avoid SYN flood
    }

    // Wait for connections to establish
    std::this_thread::sleep_for(std::chrono::seconds(3));

    int peak_connections = g_active_connections.load();
    std::cout << GREEN << "  ✓ Peak connections established: " << peak_connections
              << " / " << total_conns << RESET << "\n";
    if (g_failed_connections > 0) {
        std::cout << RED << "  ✗ Failed connections: " << g_failed_connections << RESET << "\n";
    }

    print_separator();
    std::cout << BOLD << "\nPhase 2: Sustained Load Test (" << duration << "s)...\n\n" << RESET;

    // Column headers
    std::cout << BOLD
              << std::left << std::setw(8) << "Time"
              << std::setw(12) << "Active"
              << std::setw(12) << "TX msg/s"
              << std::setw(12) << "RX read/s"
              << std::setw(12) << "Disconn"
              << std::setw(12) << "CPU%"
              << RESET << "\n";
    print_separator();

    long long last_rx = g_messages_received.load();
    long long last_tx = g_messages_sent.load();

    for (int i = 0; i < duration && g_running; ++i) {
        std::this_thread::sleep_for(std::chrono::seconds(1));

        long long current_rx = g_messages_received.load();
        long long current_tx = g_messages_sent.load();

        long long rx_qps = current_rx - last_rx;
        long long tx_qps = current_tx - last_tx;

        last_rx = current_rx;
        last_tx = current_tx;

        double cpu = cpu_tracker.readCpu();
        int active = g_active_connections.load();
        int disconn = g_disconnected.load();

        // Store snapshot
        {
            std::lock_guard<std::mutex> lock(g_snapshot_mutex);
            g_snapshots.push_back({active, g_failed_connections.load(), tx_qps, rx_qps, cpu});
        }

        // Color code CPU usage
        const char* cpu_color = GREEN;
        if (cpu > 70) cpu_color = RED;
        else if (cpu > 40) cpu_color = YELLOW;

        // Color code active connections
        const char* conn_color = GREEN;
        if (active < peak_connections * 0.8) conn_color = RED;
        else if (active < peak_connections * 0.95) conn_color = YELLOW;

        std::cout << std::left << std::setw(8) << ("[T+" + std::to_string(i+1) + "s]")
                  << conn_color << std::setw(12) << active << RESET
                  << std::setw(12) << tx_qps
                  << std::setw(12) << rx_qps
                  << std::setw(12) << disconn
                  << cpu_color << std::fixed << std::setprecision(1) << std::setw(12) << cpu << "%" << RESET
                  << "\n";
    }

    // ---- Shutdown ----
    g_running = false;
    std::cout << "\n" << BOLD << "Stopping worker threads..." << RESET << "\n";

    for (auto& t : threads) {
        if (t.joinable()) t.join();
    }

    // ============ Final Report ============
    print_header("STRESS TEST REPORT");

    long long total_tx = g_messages_sent.load();
    long long total_rx = g_messages_received.load();

    // Calculate peak QPS
    long long peak_tx_qps = 0, peak_rx_qps = 0;
    double avg_tx_qps = 0, avg_rx_qps = 0, avg_cpu = 0;
    int cpu_samples = 0;
    {
        std::lock_guard<std::mutex> lock(g_snapshot_mutex);
        for (const auto& s : g_snapshots) {
            if (s.tx_qps > peak_tx_qps) peak_tx_qps = s.tx_qps;
            if (s.rx_qps > peak_rx_qps) peak_rx_qps = s.rx_qps;
            avg_tx_qps += s.tx_qps;
            avg_rx_qps += s.rx_qps;
            if (s.cpu_usage > 0) {
                avg_cpu += s.cpu_usage;
                cpu_samples++;
            }
        }
        if (!g_snapshots.empty()) {
            avg_tx_qps /= g_snapshots.size();
            avg_rx_qps /= g_snapshots.size();
        }
        if (cpu_samples > 0) {
            avg_cpu /= cpu_samples;
        }
    }

    std::cout << BOLD << "📊 Connection Metrics:" << RESET << "\n";
    std::cout << "  Peak Active Connections:    " << GREEN << peak_connections << RESET << "\n";
    std::cout << "  Final Active Connections:   " << g_active_connections.load() << "\n";
    std::cout << "  Failed Connections:         " << g_failed_connections.load() << "\n";
    std::cout << "  Disconnected During Test:   " << g_disconnected.load() << "\n";
    std::cout << "  Login Messages Sent:        " << g_login_sent.load() << "\n";
    std::cout << "  Heartbeats Sent:            " << g_heartbeat_sent.load() << "\n";

    std::cout << "\n" << BOLD << "📈 Throughput Metrics:" << RESET << "\n";
    std::cout << "  Total Messages Sent:        " << total_tx << "\n";
    std::cout << "  Total Messages Received:    " << total_rx << "\n";
    std::cout << "  Peak TX QPS:                " << GREEN << peak_tx_qps << " msg/s" << RESET << "\n";
    std::cout << "  Peak RX QPS:                " << peak_rx_qps << " read/s\n";
    std::cout << "  Avg TX QPS:                 " << (long long)avg_tx_qps << " msg/s\n";
    std::cout << "  Avg RX QPS:                 " << (long long)avg_rx_qps << " read/s\n";

    if (target_pid > 0) {
        std::cout << "\n" << BOLD << "🖥️  Server CPU Metrics:" << RESET << "\n";
        std::cout << "  Average CPU Usage:          " << std::fixed << std::setprecision(1) << avg_cpu << "%\n";

        double max_cpu = 0;
        for (const auto& s : g_snapshots) max_cpu = std::max(max_cpu, s.cpu_usage);
        std::cout << "  Peak CPU Usage:             " << max_cpu << "%\n";
    }

    // ---- Stability Analysis ----
    print_separator();
    std::cout << "\n" << BOLD << "🔍 Stability Analysis:" << RESET << "\n\n";

    auto results = analyze_stability(g_snapshots);
    bool all_passed = true;
    for (const auto& r : results) {
        const char* icon = r.passed ? "✅" : "❌";
        const char* color = r.passed ? GREEN : RED;
        std::cout << "  " << icon << " " << color << BOLD << std::left << std::setw(25) << r.test_name << RESET
                  << " " << r.detail << "\n";
        if (!r.passed) all_passed = false;
    }

    print_separator();
    std::cout << "\n" << BOLD;
    if (all_passed) {
        std::cout << GREEN << "  ✅ ALL TESTS PASSED — Server is stable under this load!" << RESET << "\n";
    } else {
        std::cout << RED << "  ⚠️  SOME TESTS FAILED — Review results above" << RESET << "\n";
    }
    std::cout << "\n";

    return all_passed ? 0 : 1;
}
