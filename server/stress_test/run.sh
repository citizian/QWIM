#!/bin/bash
# ============================================================
# QWIM Stress Test - One-Click Runner
#
# Usage:
#   ./run.sh <server_ip> <server_port>
#   ./run.sh 192.168.1.100 8080
#   ./run.sh 10.0.0.5 8080 /path/to/report.txt
#
# This script:
#   1. Builds the stress tester (if needed)
#   2. Raises FD limits
#   3. Runs all 4 test phases automatically
#   4. Generates a report file
# ============================================================

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/build"

RED='\033[0;31m'
GREEN='\033[0;32m'
CYAN='\033[0;36m'
BOLD='\033[1m'
RESET='\033[0m'

if [ $# -lt 2 ]; then
    echo -e "${BOLD}QWIM Server Stress Test${RESET}"
    echo ""
    echo "Usage: $0 <server_ip> <server_port> [report_path]"
    echo ""
    echo "  server_ip    IP address of the remote QWIM server"
    echo "  server_port  Port of the remote QWIM server"
    echo "  report_path  (optional) Output report file (default: stress_report.txt)"
    echo ""
    echo "Examples:"
    echo "  $0 192.168.1.100 8080"
    echo "  $0 10.0.0.5 8080 /tmp/my_report.txt"
    echo ""
    exit 1
fi

SERVER_IP="$1"
SERVER_PORT="$2"
REPORT_PATH="${3:-$SCRIPT_DIR/stress_report.txt}"

# ---- Build ----
echo -e "${BOLD}${CYAN}[1/3] Building stress tester...${RESET}"
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"
cmake .. -DCMAKE_BUILD_TYPE=Release > /dev/null 2>&1
make -j$(nproc) 2>&1 | tail -3

if [ ! -f "$BUILD_DIR/stress_tester" ]; then
    echo -e "${RED}Build failed!${RESET}"
    exit 1
fi
echo -e "${GREEN}✓ Build OK${RESET}\n"

# ---- Raise FD limit ----
echo -e "${BOLD}${CYAN}[2/3] Preparing environment...${RESET}"
ulimit -n 65535 2>/dev/null || ulimit -n 10240 2>/dev/null || true
echo -e "  FD limit: $(ulimit -n)"
echo -e "${GREEN}✓ Ready${RESET}\n"

# ---- Run ----
echo -e "${BOLD}${CYAN}[3/3] Running stress tests against ${SERVER_IP}:${SERVER_PORT}${RESET}"
echo -e "  Report will be saved to: ${REPORT_PATH}\n"

"$BUILD_DIR/stress_tester" "$SERVER_IP" "$SERVER_PORT" "$REPORT_PATH"
EXIT_CODE=$?

echo ""
if [ $EXIT_CODE -eq 0 ]; then
    echo -e "${GREEN}${BOLD}Done! All tests passed.${RESET}"
else
    echo -e "${RED}${BOLD}Done. Some tests failed — check the report.${RESET}"
fi
echo -e "Report: ${BOLD}${REPORT_PATH}${RESET}"

exit $EXIT_CODE
