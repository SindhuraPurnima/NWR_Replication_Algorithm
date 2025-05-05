#!/bin/bash

# Weak scaling test for replication algorithm
# This script tests how the system performs as load increases

echo "Starting weak scaling test..."

# Clean up any existing servers
pkill -f "./build/server" || true

# Start the servers
cd /Users/sindhurapurnima/Desktop/mini3/mini3
echo "Starting 5 servers..."
./build/server config_server1.json > server1.log 2>&1 &
./build/server config_server2.json > server2.log 2>&1 &
./build/server config_server3.json > server3.log 2>&1 &
./build/server config_server4.json > server4.log 2>&1 &
./build/server config_server5.json > server5.log 2>&1 &

# Wait for servers to initialize
echo "Waiting for servers to initialize..."
sleep 5

# Run tests with increasing load
echo "Running weak scaling tests with increasing load..."

# Function to test with specific client count and report time
test_with_clients() {
    local client_count=$1
    echo "Testing with $client_count concurrent clients..."
    
    # Start time
    start_time=$(date +%s.%N)
    
    # Launch clients
    for ((i=1; i<=$client_count; i++)); do
        ./build/client localhost:50051 localhost:50052 localhost:50053 localhost:50054 localhost:50055 > /dev/null &
    done
    
    # Wait for all clients to finish
    wait
    
    # End time
    end_time=$(date +%s.%N)
    
    # Calculate duration
    duration=$(echo "$end_time - $start_time" | bc)
    
    echo "Test with $client_count clients completed in $duration seconds"
    echo "$client_count,$duration" >> weak_scaling_results.csv
    
    # Wait between tests
    sleep 2
}

# Create results file
echo "clients,duration" > weak_scaling_results.csv

# Test with increasing client counts
test_with_clients 1
test_with_clients 2
test_with_clients 5
test_with_clients 10
test_with_clients 20
test_with_clients 50

# Clean up
echo "Cleaning up..."
pkill -f "./build/server"

echo "Weak scaling test completed. Results saved to weak_scaling_results.csv"
echo "Check server logs for equalization rate and consistency metrics" 