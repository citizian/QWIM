#!/bin/bash
pkill -9 server_bin || true
sleep 1
./build/server_bin &
SERVER_PID=$!
sleep 1
./build/client_bin <<'EOF'
ReactorUser
/help
/list
This is a test of the EventLoop!
/quit
EOF
sleep 1
cat logs/server.log
kill -9 $SERVER_PID
