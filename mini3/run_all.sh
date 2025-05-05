#!/bin/bash

# Build the project
cd "$(dirname "$0")"
mkdir -p build
cd build
cmake ..
make -j4

# Create logs directory
cd ..
mkdir -p logs

# Run each server in a separate terminal with output redirected to log files
osascript -e 'tell application "Terminal" to do script "cd \"'$(pwd)'\" && ./build/server config_server1.json > logs/server1.log 2>&1"'
sleep 1
osascript -e 'tell application "Terminal" to do script "cd \"'$(pwd)'\" && ./build/server config_server2.json > logs/server2.log 2>&1"'
sleep 1
osascript -e 'tell application "Terminal" to do script "cd \"'$(pwd)'\" && ./build/server config_server3.json > logs/server3.log 2>&1"'
sleep 1
osascript -e 'tell application "Terminal" to do script "cd \"'$(pwd)'\" && ./build/server config_server4.json > logs/server4.log 2>&1"'
sleep 1
osascript -e 'tell application "Terminal" to do script "cd \"'$(pwd)'\" && ./build/server config_server5.json > logs/server5.log 2>&1"'
sleep 2

# Run the metrics monitor with output redirected to log file
osascript -e 'tell application "Terminal" to do script "cd \"'$(pwd)'\" && ./build/metrics_monitor > logs/metrics_monitor.log 2>&1"'
sleep 2

# Run the client to generate load with output redirected to log file
osascript -e 'tell application "Terminal" to do script "cd \"'$(pwd)'\" && ./build/client localhost:50051 > logs/client.log 2>&1"'

echo "All components started. Logs are being written to the logs directory."
echo "You can monitor logs in real-time using:"
echo "  tail -f logs/server1.log"
echo "  tail -f logs/metrics_monitor.log"
echo "  tail -f logs/client.log"
echo "Press Ctrl+C to exit."
read -r 