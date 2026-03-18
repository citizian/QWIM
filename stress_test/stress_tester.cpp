#include <iostream>
#include <vector>
#include <thread>
#include <atomic>
#include <chrono>
#include <string>
#include <cstring>
#include <cmath>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <sys/resource.h>
#include <sys/poll.h>
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

// Per-second snapshot
struct Snapshot {
    int second;
    int active_conns;
    int failed_conns;
    long long tx_qps;
    long long rx_qps;
};
std::mutex g_snapshot_mutex;
std::vector<Snapshot> g_snapshots;

// ============ Utility ============
int create_nonblocking_socket() {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd == -1) return -1;
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    return fd;
}

bool wait_for_connect(int fd, int timeout_ms) {
    struct pollfd pfd;
    pfd.fd = fd;
    pfd.events = POLLOUT;
    int ret = poll(&pfd, 1, timeout_ms);
    if (ret > 0) {
        int so_error = 0;
        socklen_t len = sizeof(so_error);
        if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &so_error, &len) == 0) {
            return so_error == 0;
        }
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

// ============ Worker Thread ============
void worker_thread(int thread_id, const std::string& ip, int port,
                   int num_connections, int messages_per_sec) {
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
        if (fd == -1) { g_failed_connections++; g_connect_errors++; continue; }

        int ret = connect(fd, (struct sockaddr*)&server_addr, sizeof(server_addr));
        if (ret == -1 && errno != EINPROGRESS) {
            close(fd); g_failed_connections++; g_connect_errors++; continue;
        }

        if (!wait_for_connect(fd, 3000)) {
            close(fd); g_failed_connections++; g_connect_errors++; continue;
        }

        struct epoll_event ev;
        ev.events = EPOLLIN | EPOLLOUT | EPOLLET;
        ev.data.fd = fd;
        if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &ev) == 0) {
            fds.push_back(fd);
            g_active_connections++;

            // Send login
            std::string user = "stress_" + std::to_string(thread_id) + "_" + std::to_string(i);
            std::string login_json = R"({"type":"login","user":")" + user + R"("})";
            send_json_packet(fd, login_json);
            g_login_sent++;
        } else {
            close(fd); g_failed_connections++;
        }

        // Stagger connections
        if (i % 50 == 49) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    // ---- Prepare chat payload ----
    std::string chat_payload = R"({"type":"chat","msg":"stress_test_message"})";

    auto last_send_time = std::chrono::steady_clock::now();
    auto last_heartbeat_time = std::chrono::steady_clock::now();
    double send_interval = messages_per_sec > 0 ? (1.0 / messages_per_sec) : 0;

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
                g_active_connections--; g_disconnected++;
                continue;
            }
            if (events[i].events & EPOLLIN) {
                char buf[4096];
                while (true) {
                    ssize_t bytes = read(efd, buf, sizeof(buf));
                    if (bytes > 0) { g_messages_received++; }
                    else if (bytes == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)) { break; }
                    else {
                        epoll_ctl(epoll_fd, EPOLL_CTL_DEL, efd, nullptr);
                        close(efd);
                        fds.erase(std::remove(fds.begin(), fds.end(), efd), fds.end());
                        g_active_connections--; g_disconnected++;
                        break;
                    }
                }
            }
        }

        auto now = std::chrono::steady_clock::now();

        // Send chat messages
        if (messages_per_sec > 0 && std::chrono::duration<double>(now - last_send_time).count() >= send_interval) {
            last_send_time = now;
            for (int fd : fds) {
                send_json_packet(fd, chat_payload);
                g_messages_sent++;
            }
        }

        // Send heartbeat every 10s
        if (std::chrono::duration<double>(now - last_heartbeat_time).count() >= 10.0) {
            last_heartbeat_time = now;
            std::string hb = R"({"type":"heartbeat"})";
            for (int fd : fds) {
                send_json_packet(fd, hb);
                g_heartbeat_sent++;
            }
        }
    }

    for (int fd : fds) close(fd);
    close(epoll_fd);
}

// ============ Colors ============
const char* RESET  = "\033[0m";
const char* BOLD   = "\033[1m";
const char* RED    = "\033[31m";
const char* GREEN  = "\033[32m";
const char* YELLOW = "\033[33m";
const char* CYAN   = "\033[36m";

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

// ============ SIGNAL ============
void signal_handler(int) { g_running = false; }

// ============ Run one test phase, return snapshots ============
struct PhaseResult {
    std::string name;
    int target_conns;
    int peak_conns;
    int failed_conns;
    int final_conns;
    int disconnected;
    long long total_tx;
    long long total_rx;
    long long peak_tx_qps;
    long long peak_rx_qps;
    double avg_tx_qps;
    double avg_rx_qps;
    int duration;
    std::vector<Snapshot> snapshots;
    bool conn_stable;
    bool qps_stable;
    double qps_cv;
};

PhaseResult run_phase(const std::string& name, const std::string& ip, int port,
                      int conns_per_thread, int num_threads, int msgs_per_sec, int duration) {
    // Reset all globals
    g_messages_received = 0;
    g_messages_sent = 0;
    g_active_connections = 0;
    g_failed_connections = 0;
    g_connect_errors = 0;
    g_login_sent = 0;
    g_heartbeat_sent = 0;
    g_disconnected = 0;
    g_running = true;
    {
        std::lock_guard<std::mutex> lock(g_snapshot_mutex);
        g_snapshots.clear();
    }

    int total_conns = num_threads * conns_per_thread;
    print_header(name);

    std::cout << "  Target:        " << ip << ":" << port << "\n";
    std::cout << "  Threads:       " << num_threads << "\n";
    std::cout << "  Conns/Thread:  " << conns_per_thread << "\n";
    std::cout << "  Total Conns:   " << total_conns << "\n";
    std::cout << "  Msg Rate:      " << msgs_per_sec << " msg/conn/s\n";
    std::cout << "  Duration:      " << duration << "s\n\n";

    // Launch workers
    std::vector<std::thread> threads;
    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back(worker_thread, i, ip, port, conns_per_thread, msgs_per_sec);
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    // Wait for connections
    std::this_thread::sleep_for(std::chrono::seconds(3));
    int peak_connections = g_active_connections.load();
    std::cout << GREEN << "  ✓ Connections established: " << peak_connections
              << " / " << total_conns << RESET << "\n";
    if (g_failed_connections > 0) {
        std::cout << RED << "  ✗ Failed: " << g_failed_connections << RESET << "\n";
    }
    print_separator();

    // Column headers
    std::cout << BOLD
              << std::left << std::setw(8) << "Time"
              << std::setw(12) << "Active"
              << std::setw(14) << "TX msg/s"
              << std::setw(14) << "RX read/s"
              << std::setw(10) << "Disconn"
              << RESET << "\n";
    print_separator();

    long long last_rx = g_messages_received.load();
    long long last_tx = g_messages_sent.load();

    for (int i = 0; i < duration && g_running; ++i) {
        std::this_thread::sleep_for(std::chrono::seconds(1));

        long long cur_rx = g_messages_received.load();
        long long cur_tx = g_messages_sent.load();
        long long rx_qps = cur_rx - last_rx;
        long long tx_qps = cur_tx - last_tx;
        last_rx = cur_rx;
        last_tx = cur_tx;

        int active = g_active_connections.load();
        int disconn = g_disconnected.load();

        {
            std::lock_guard<std::mutex> lock(g_snapshot_mutex);
            g_snapshots.push_back({i + 1, active, g_failed_connections.load(), tx_qps, rx_qps});
        }

        const char* conn_color = (active < peak_connections * 0.8) ? RED :
                                 (active < peak_connections * 0.95) ? YELLOW : GREEN;

        std::cout << std::left << std::setw(8) << ("[T+" + std::to_string(i + 1) + "s]")
                  << conn_color << std::setw(12) << active << RESET
                  << std::setw(14) << tx_qps
                  << std::setw(14) << rx_qps
                  << std::setw(10) << disconn
                  << "\n";
    }

    g_running = false;
    std::cout << "\n  Stopping threads...\n";
    for (auto& t : threads) if (t.joinable()) t.join();

    // Build result
    PhaseResult res;
    res.name = name;
    res.target_conns = total_conns;
    res.peak_conns = peak_connections;
    res.failed_conns = g_failed_connections.load();
    res.final_conns = g_active_connections.load();
    res.disconnected = g_disconnected.load();
    res.total_tx = g_messages_sent.load();
    res.total_rx = g_messages_received.load();
    res.duration = duration;
    res.peak_tx_qps = 0;
    res.peak_rx_qps = 0;
    res.avg_tx_qps = 0;
    res.avg_rx_qps = 0;

    {
        std::lock_guard<std::mutex> lock(g_snapshot_mutex);
        res.snapshots = g_snapshots;
    }

    for (const auto& s : res.snapshots) {
        if (s.tx_qps > res.peak_tx_qps) res.peak_tx_qps = s.tx_qps;
        if (s.rx_qps > res.peak_rx_qps) res.peak_rx_qps = s.rx_qps;
        res.avg_tx_qps += s.tx_qps;
        res.avg_rx_qps += s.rx_qps;
    }
    if (!res.snapshots.empty()) {
        res.avg_tx_qps /= res.snapshots.size();
        res.avg_rx_qps /= res.snapshots.size();
    }

    // Connection stability
    double conn_drop = res.peak_conns > 0 ? 100.0 * (res.peak_conns - res.final_conns) / res.peak_conns : 0;
    res.conn_stable = conn_drop < 10.0;

    // QPS stability (CV, skip first 5s warmup)
    std::vector<double> tx_vals;
    for (size_t i = 5; i < res.snapshots.size(); ++i) tx_vals.push_back(res.snapshots[i].tx_qps);
    if (!tx_vals.empty()) {
        double mean = std::accumulate(tx_vals.begin(), tx_vals.end(), 0.0) / tx_vals.size();
        double sq = 0;
        for (double v : tx_vals) sq += (v - mean) * (v - mean);
        double sd = std::sqrt(sq / tx_vals.size());
        res.qps_cv = mean > 0 ? sd / mean : 0;
    } else {
        res.qps_cv = 0;
    }
    res.qps_stable = res.qps_cv < 0.3;

    std::cout << GREEN << "  ✓ Phase complete\n" << RESET;
    return res;
}

// ============ Report Generator ============
void generate_report(const std::string& filepath,
                     const std::string& ip, int port,
                     const std::vector<PhaseResult>& results) {
    std::ofstream f(filepath);
    if (!f.is_open()) {
        std::cerr << RED << "  ✗ Cannot write report to " << filepath << RESET << "\n";
        return;
    }

    // Timestamp
    auto now = std::chrono::system_clock::now();
    auto t = std::chrono::system_clock::to_time_t(now);
    char timebuf[64];
    strftime(timebuf, sizeof(timebuf), "%Y-%m-%d %H:%M:%S", localtime(&t));

    f << "================================================================\n";
    f << "         QWIM Server Stress Test Report\n";
    f << "================================================================\n\n";
    f << "Date:         " << timebuf << "\n";
    f << "Target:       " << ip << ":" << port << "\n";

    // FD limit
    struct rlimit lim;
    getrlimit(RLIMIT_NOFILE, &lim);
    f << "FD Limit:     " << lim.rlim_cur << " (soft) / " << lim.rlim_max << " (hard)\n\n";

    // Summary table
    f << "────────────────────────────────────────────────────────────────\n";
    f << "SUMMARY\n";
    f << "────────────────────────────────────────────────────────────────\n\n";

    bool all_pass = true;
    for (const auto& r : results) {
        bool pass = r.conn_stable && r.qps_stable;
        if (!pass) all_pass = false;
        f << (pass ? "[PASS]" : "[FAIL]") << " " << r.name << "\n";
    }
    f << "\nOverall: " << (all_pass ? "ALL TESTS PASSED" : "SOME TESTS FAILED") << "\n\n";

    // Detailed results
    for (const auto& r : results) {
        f << "════════════════════════════════════════════════════════════════\n";
        f << r.name << "\n";
        f << "════════════════════════════════════════════════════════════════\n\n";
        f << "  Connections:     " << r.peak_conns << " / " << r.target_conns << " established\n";
        f << "  Failed:          " << r.failed_conns << "\n";
        f << "  Final Active:    " << r.final_conns << "\n";
        f << "  Disconnected:    " << r.disconnected << "\n";
        f << "  Duration:        " << r.duration << "s\n";
        f << "  Total TX:        " << r.total_tx << " messages\n";
        f << "  Total RX:        " << r.total_rx << " reads\n";
        f << "  Peak TX QPS:     " << r.peak_tx_qps << " msg/s\n";
        f << "  Avg TX QPS:      " << (long long)r.avg_tx_qps << " msg/s\n";
        f << "  Peak RX QPS:     " << r.peak_rx_qps << " read/s\n";
        f << "  Avg RX QPS:      " << (long long)r.avg_rx_qps << " read/s\n";
        f << "  QPS CV:          " << std::fixed << std::setprecision(4) << r.qps_cv << "\n";
        f << "  Conn Stable:     " << (r.conn_stable ? "YES" : "NO") << "\n";
        f << "  QPS Stable:      " << (r.qps_stable  ? "YES" : "NO") << "\n";

        f << "\n  -- Per-Second Data --\n";
        f << "  " << std::left << std::setw(8) << "Time"
          << std::setw(12) << "Active"
          << std::setw(14) << "TX msg/s"
          << std::setw(14) << "RX read/s"
          << "\n";
        for (const auto& s : r.snapshots) {
            f << "  " << std::left << std::setw(8) << ("[T+" + std::to_string(s.second) + "s]")
              << std::setw(12) << s.active_conns
              << std::setw(14) << s.tx_qps
              << std::setw(14) << s.rx_qps
              << "\n";
        }
        f << "\n";
    }

    f << "================================================================\n";
    f << "End of Report\n";
    f << "================================================================\n";
    f.close();
}

// ============ Main ============
int main(int argc, char* argv[]) {
    signal(SIGPIPE, SIG_IGN);
    signal(SIGINT, signal_handler);

    if (argc < 3) {
        std::cerr << "Usage: " << argv[0] << " <server_ip> <server_port> [report_path]\n\n"
                  << "  Runs 4 automated stress test phases against a remote QWIM server.\n"
                  << "  No manual intervention needed.\n\n"
                  << "  server_ip    - IP address of the QWIM server\n"
                  << "  server_port  - Port of the QWIM server\n"
                  << "  report_path  - (optional) Output report file path\n"
                  << "                 Default: ./stress_report.txt\n\n"
                  << "Example:\n"
                  << "  " << argv[0] << " 192.168.1.100 8080\n"
                  << "  " << argv[0] << " 10.0.0.5 8080 /tmp/report.txt\n";
        return 1;
    }

    std::string ip = argv[1];
    int port = std::stoi(argv[2]);
    std::string report_path = (argc > 3) ? argv[3] : "stress_report.txt";

    // Check FD limit
    struct rlimit lim;
    getrlimit(RLIMIT_NOFILE, &lim);

    print_header("QWIM Server Stress Test Suite");
    std::cout << "  Target Server:    " << ip << ":" << port << "\n";
    std::cout << "  Report Output:    " << report_path << "\n";
    std::cout << "  FD Limit (soft):  " << lim.rlim_cur << "\n\n";
    std::cout << "  This test runs 4 phases automatically.\n";
    std::cout << "  Total estimated time: ~3 minutes.\n";
    print_separator();

    // Quick connectivity check
    {
        int fd = socket(AF_INET, SOCK_STREAM, 0);
        struct sockaddr_in sa{};
        sa.sin_family = AF_INET;
        sa.sin_port = htons(port);
        inet_pton(AF_INET, ip.c_str(), &sa.sin_addr);

        struct timeval tv{3, 0};
        setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

        if (connect(fd, (struct sockaddr*)&sa, sizeof(sa)) != 0) {
            std::cerr << RED << "\n  ✗ Cannot connect to " << ip << ":" << port
                      << " — is the server running?\n" << RESET;
            close(fd);
            return 1;
        }
        close(fd);
        std::cout << GREEN << "  ✓ Server is reachable\n" << RESET;
    }

    std::vector<PhaseResult> results;

    // ---- Phase 1: Max Connections ----
    {
        int max_conns = (int)lim.rlim_cur - 200;
        if (max_conns > 5000) max_conns = 5000;
        if (max_conns < 100) max_conns = 100;
        int per_thread = max_conns / 4;

        results.push_back(run_phase(
            "Phase 1: Max Connections",
            ip, port, per_thread, 4, 0, 20));

        std::this_thread::sleep_for(std::chrono::seconds(3));
    }

    // ---- Phase 2: QPS Throughput ----
    results.push_back(run_phase(
        "Phase 2: QPS Throughput",
        ip, port, 50, 4, 10, 30));

    std::this_thread::sleep_for(std::chrono::seconds(3));

    // ---- Phase 3: High Load (CPU stress) ----
    results.push_back(run_phase(
        "Phase 3: High Load",
        ip, port, 25, 4, 50, 30));

    std::this_thread::sleep_for(std::chrono::seconds(3));

    // ---- Phase 4: Stability (sustained) ----
    results.push_back(run_phase(
        "Phase 4: Stability (60s sustained)",
        ip, port, 125, 4, 5, 60));

    // ============ Generate Report ============
    generate_report(report_path, ip, port, results);

    // ============ Print Summary ============
    print_header("FINAL RESULTS");

    bool all_pass = true;
    for (const auto& r : results) {
        bool pass = r.conn_stable && r.qps_stable;
        const char* icon = pass ? "✅" : "❌";
        const char* color = pass ? GREEN : RED;
        if (!pass) all_pass = false;

        std::cout << "  " << icon << " " << color << BOLD
                  << std::left << std::setw(40) << r.name << RESET;

        std::cout << "  Conns: " << r.peak_conns << "/" << r.target_conns;
        if (r.avg_tx_qps > 0) {
            std::cout << "  TX: " << (long long)r.avg_tx_qps << " msg/s";
        }
        std::cout << "\n";
    }

    print_separator();
    if (all_pass) {
        std::cout << "\n  " << GREEN << BOLD << "ALL TESTS PASSED ✅" << RESET << "\n";
    } else {
        std::cout << "\n  " << RED << BOLD << "SOME TESTS FAILED ⚠️" << RESET << "\n";
    }

    std::cout << "\n  Report saved to: " << BOLD << report_path << RESET << "\n\n";
    return all_pass ? 0 : 1;
}
