#!/bin/bash

# Strong scaling test for replication algorithm
# This script tests how the system performs with the same workload on different numbers of servers

echo "Starting strong scaling test..."

# Function to test with specific server count
test_with_servers() {
    local server_count=$1
    echo "Testing with $server_count servers..."
    
    # Clean up any existing servers
    pkill -f "./build/server" || true
    
    # Start the specified number of servers
    cd /Users/sindhurapurnima/Desktop/mini3/mini3
    for ((i=1; i<=$server_count; i++)); do
        echo "Starting server $i..."
        ./build/server config_server$i.json > server$i.log 2>&1 &
    done
    
    # Wait for servers to initialize
    echo "Waiting for servers to initialize..."
    sleep 5
    
    # Build the server list
    SERVER_LIST=""
    for ((i=1; i<=$server_count; i++)); do
        SERVER_LIST="$SERVER_LIST localhost:$((50050 + $i))"
    done
    
    # Fixed workload: 20 clients
    echo "Running fixed workload with 20 clients on $server_count servers..."
    
    # Start time
    start_time=$(date +%s.%N)
    
    # Launch clients
    for ((i=1; i<=20; i++)); do
        ./build/client $SERVER_LIST > /dev/null &
    done
    
    # Wait for all clients to finish
    wait
    
    # End time
    end_time=$(date +%s.%N)
    
    # Calculate duration
    duration=$(echo "$end_time - $start_time" | bc)
    
    echo "Test with $server_count servers completed in $duration seconds"
    echo "$server_count,$duration" >> strong_scaling_results.csv
    
    # Gather metrics from server logs
    total_work_stolen=0
    total_conflicts=0
    
    for ((i=1; i<=$server_count; i++)); do
        # Count work stealing events
        stolen=$(grep -c "Stole " server$i.log || echo 0)
        total_work_stolen=$((total_work_stolen + stolen))
        
        # Count conflict resolution events
        conflicts=$(grep -c "CONFLICT RESOLVED" server$i.log || echo 0)
        total_conflicts=$((total_conflicts + conflicts))
    done
    
    echo "Total work stealing events: $total_work_stolen"
    echo "Total conflict resolution events: $total_conflicts"
    echo "$server_count,$duration,$total_work_stolen,$total_conflicts" >> strong_scaling_detailed.csv
    
    # Wait between tests
    sleep 2
}

# Create results files
echo "servers,duration" > strong_scaling_results.csv
echo "servers,duration,work_stealing_events,conflict_resolutions" > strong_scaling_detailed.csv

# Test with increasing server counts
test_with_servers 1
test_with_servers 2
test_with_servers 3
test_with_servers 4
test_with_servers 5

# Clean up
echo "Cleaning up..."
pkill -f "./build/server"

echo "Strong scaling test completed. Results saved to strong_scaling_results.csv and strong_scaling_detailed.csv" 