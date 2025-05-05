# Distributed Replication System with Work Stealing

This project implements a distributed replication system with dynamic work stealing and load balancing using a weighted rank calculation algorithm. The system is built using C++ and gRPC for efficient inter-process communication.

## Algorithm Overview

The system implements an adaptive work stealing algorithm that uses a weighted scoring system to make decisions about work distribution. The algorithm considers multiple factors to calculate a server's rank:

### Ranking Formula
```
Rank = (c1 × queue_size) + (c2 × cpu_load) + (c3 × memory_usage) + (c4 × network_latency) + (c5 × distance)
```

Where:
- Each coefficient (c1-c5) represents the weight of that factor
- Factors are normalized to produce a rank between 0 and 1
- Higher rank indicates a more heavily loaded server

The algorithm uses this rank to make decisions about:
- When to redistribute work (based on stealing threshold)
- Which servers to steal from
- How much work to redistribute
- Where to forward new collision data

## Project Structure and Components

### Core Components

#### 1. Server (`cpp/src/server.cpp`)
- Implements the core server functionality
- Manages connections with other servers
- Handles work stealing and distribution
- Contains key methods:
  - `replicateData()`: Replicates collision data to other servers
  - `calculateServerRank()`: Computes server rank using weighted metrics
  - `shouldRedistributeWork()`: Determines if a server should redistribute work
  - `ForwardData()`: Forwards collision data based on server loads

#### 2. Client (`cpp/src/client.cpp`)
- Client application for generating collision data
- Sends data to the entry point server
- Configurable for testing work stealing

#### 3. MetricsMonitor (`cpp/src/metrics_monitor.cpp`)
- Monitors server metrics in real-time
- Displays CPU, memory, queue, latency, and rank
- Shows which servers are online/offline

#### 4. Configuration Files (`config_server*.json`)
- Controls server behavior and initial metrics
- Sets work stealing thresholds and weights
- Defines network topology for simulation

### Visualization Tools

#### Metrics Visualization (`Visualize_metrics.py`)
- Parses metrics monitor logs
- Generates time-series charts of server performance
- Shows how work stealing affects server loads
- Calculates performance statistics

## Configuration

Each server is configured using a JSON file with the following structure:

```json
{
    "server_id": "server1",
    "address": "localhost",
    "port": 50051,
    "is_entry_point": true,
    "work_stealing": {
        "enabled": true,
        "stealing_threshold": 0.4,
        "weights": {
            "queue_size": 0.3,
            "cpu_load": 0.2,
            "memory_usage": 0.2,
            "network_latency": 0.2,
            "distance": 0.1
        }
    },
    "network_simulation": {
        "base_latency": 50.0,
        "max_acceptable_latency": 100.0,
        "latency_variation": 20.0,
        "topology": {
            "server2": 1,
            "server3": 1,
            "server4": 1,
            "server5": 1
        }
    },
    "metrics": {
        "collection_interval_ms": 1000,
        "max_history_size": 100,
        "max_queue_size": 1000,
        "dynamic_update": true,
        "initial_values": {
            "cpu_utilization": 0.30,
            "memory_usage": 0.25,
            "message_queue_length": 200,
            "network_latency": 45.0
        }
    }
}
```

## Building and Running

### Prerequisites
- C++17 compatible compiler
- CMake 3.10 or higher
- gRPC and Protocol Buffers
- Python 3.x with pandas, matplotlib, and numpy (for visualization)

### Building the Project

```bash
mkdir build
cd build
cmake ..
make
```

### Running the System

#### Quick Start with Automation Script
The included `run_all.sh` script automates starting all components:

```bash
chmod +x run_all.sh
./run_all.sh
```

This starts 5 servers, the metrics monitor, and a client in separate terminals.

#### Manual Execution
1. Start each server individually:
```bash
./build/server config_server1.json
./build/server config_server2.json
# etc.
```

2. Start the metrics monitor:
```bash
./build/metrics_monitor
```

3. Run the client:
```bash
./build/client localhost:50051
```

4. Visualize the results:
```bash
python Visualize_metrics.py
```

## Work Stealing Implementation

The system implements work stealing with the following features:

1. **Dynamic Load Balancing**:
   - Servers continuously monitor their load metrics
   - When a server's rank exceeds the stealing threshold, it attempts to redistribute work
   - Work is stolen by servers with the lowest computed ranks

2. **Configurable Thresholds**:
   - Each server can have its own stealing threshold
   - Weights for different metrics can be adjusted
   - Network topology can be simulated with hop distances

3. **Data Replication**:
   - Collision data is replicated to ensure reliability
   - Replication targets are selected based on server loads
   - A minimum number of replicas is maintained

## Visualization and Analysis

The `Visualize_metrics.py` script provides:

1. Time-series plots of:
   - CPU utilization
   - Memory usage
   - Queue lengths
   - Network latency
   - Server ranks

2. Statistical analysis:
   - Average and maximum values for each metric
   - Online/offline status tracking
   - Uptime percentage

3. Automated focus on relevant time periods:
   - Automatically identifies periods when multiple servers are online
   - Highlights regions where work stealing is occurring

## Troubleshooting

Common issues:

1. **No work stealing observed**:
   - Check if stealing thresholds are appropriate
   - Ensure initial metrics create sufficient load imbalance
   - Verify network topology is correctly configured

2. **Servers going offline**:
   - Increase health check timeout values
   - Check for excessive load causing timeouts
   - Ensure proper error handling

3. **Uneven work distribution**:
   - Adjust the weights in the rank formula
   - Set uniform initial metrics across secondary servers
   - Check the ForwardData logic for routing decisions

## License

This project is licensed under the MIT License - see the LICENSE file for details. 