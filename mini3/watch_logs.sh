#!/bin/bash

# Check if log name is provided
if [ "$#" -lt 1 ]; then
    echo "Usage: $0 <log_name> [num_lines]"
    echo "Available logs: server1, server2, server3, server4, server5, metrics_monitor, client, all"
    echo "Example: $0 server1"
    echo "Example: $0 all 20"
    exit 1
fi

LOG_NAME=$1
NUM_LINES=${2:-10}  # Default to 10 lines if not specified

# Create logs directory if it doesn't exist
mkdir -p logs

# Function to check if a log file exists
check_log_file() {
    if [ ! -f "logs/$1.log" ]; then
        echo "Log file logs/$1.log does not exist yet. Waiting for it to be created..."
        touch "logs/$1.log"  # Create empty file
    fi
}

# Watch specific log or all logs
if [ "$LOG_NAME" == "all" ]; then
    # Watch all logs
    check_log_file "server1"
    check_log_file "server2"
    check_log_file "server3"
    check_log_file "server4"
    check_log_file "server5"
    check_log_file "metrics_monitor"
    check_log_file "client"
    
    # Use multitail if available, otherwise fallback to tail
    if command -v multitail &> /dev/null; then
        multitail -n $NUM_LINES logs/server1.log logs/server2.log logs/server3.log logs/server4.log logs/server5.log logs/metrics_monitor.log logs/client.log
    else
        echo "multitail not found. Using regular tail. Install multitail for better experience."
        echo "Showing last $NUM_LINES lines of each log file and following updates..."
        tail -n $NUM_LINES -f logs/server1.log logs/server2.log logs/server3.log logs/server4.log logs/server5.log logs/metrics_monitor.log logs/client.log
    fi
else
    # Watch specific log
    check_log_file "$LOG_NAME"
    echo "Showing last $NUM_LINES lines of logs/$LOG_NAME.log and following updates..."
    tail -n $NUM_LINES -f "logs/$LOG_NAME.log"
fi 