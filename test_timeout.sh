#!/bin/bash
pkill -9 server_bin || true
sleep 1
# Start server with 5 second timeout for quick testing
echo "heartbeat_timeout=5" > config/server.conf
./build/server_bin &
SERVER_PID=$!
sleep 1

# Connect with netcat, send nothing, wait 7 seconds
echo "Connecting and waiting..."
nc 127.0.0.1 8080 &
NC_PID=$!

sleep 7
# Check logs to see if it was disconnected
grep "heartbeat timeout. Disconnecting." logs/server.log

kill -9 $SERVER_PID
kill -9 $NC_PID || true
echo "heartbeat_timeout=30" > config/server.conf
