import re
import pandas as pd
import matplotlib.pyplot as plt
from datetime import datetime
import numpy as np

def parse_metrics_log(file_path):
    # Regular expressions for extracting information
    time_pattern = r"======== SERVER STATUS \((\d+:\d+:\d+)\) ========"
    server_pattern = r"\[(ONLINE|OFFLINE)\] Server: ([a-zA-Z0-9]+)"
    cpu_pattern = r"CPU: (\d+\.\d+)%"
    memory_pattern = r"Memory: (\d+\.\d+)%"
    queue_pattern = r"Queue: (\d+)/(\d+)"
    latency_pattern = r"Latency: (\d+\.\d+)ms"
    rank_pattern = r"Rank: (\d+\.\d+)"
    
    data = []
    current_time = None
    
    with open(file_path, 'r') as f:
        for line in f:
            # Extract timestamp
            time_match = re.search(time_pattern, line)
            if time_match:
                current_time = time_match.group(1)
                continue
                
            # Extract server status
            server_match = re.search(server_pattern, line)
            if server_match and current_time:
                status = server_match.group(1)
                server_id = server_match.group(2)
                
                # Skip to next line if server is offline
                if status == "OFFLINE":
                    data.append({
                        'timestamp': current_time,
                        'server_id': server_id,
                        'status': 'OFFLINE',
                        'cpu': np.nan,
                        'memory': np.nan,
                        'queue': np.nan,
                        'queue_max': np.nan,
                        'latency': np.nan,
                        'rank': np.nan
                    })
                    continue
                
                # Extract metrics for online servers
                cpu = memory = queue = queue_max = latency = rank = np.nan
                
                # Read next 5 lines for metrics
                metrics_lines = []
                for _ in range(5):
                    try:
                        metrics_lines.append(next(f))
                    except StopIteration:
                        break
                
                metrics_text = ''.join(metrics_lines)
                
                # Extract individual metrics
                cpu_match = re.search(cpu_pattern, metrics_text)
                if cpu_match:
                    cpu = float(cpu_match.group(1))
                
                memory_match = re.search(memory_pattern, metrics_text)
                if memory_match:
                    memory = float(memory_match.group(1))
                
                queue_match = re.search(queue_pattern, metrics_text)
                if queue_match:
                    queue = int(queue_match.group(1))
                    queue_max = int(queue_match.group(2))
                
                latency_match = re.search(latency_pattern, metrics_text)
                if latency_match:
                    latency = float(latency_match.group(1))
                
                rank_match = re.search(rank_pattern, metrics_text)
                if rank_match:
                    rank = float(rank_match.group(1))
                
                # Add data point
                data.append({
                    'timestamp': current_time,
                    'server_id': server_id,
                    'status': 'ONLINE',
                    'cpu': cpu,
                    'memory': memory,
                    'queue': queue,
                    'queue_max': queue_max,
                    'latency': latency,
                    'rank': rank
                })
    
    # Convert to DataFrame
    df = pd.DataFrame(data)
    
    # Convert timestamp to datetime
    df['timestamp'] = pd.to_datetime(df['timestamp'], format='%H:%M:%S')
    
    return df

def analyze_and_plot(df):
    # Find periods where at least 2 servers are online
    timestamps = df['timestamp'].unique()
    valid_timestamps = []
    
    for ts in timestamps:
        online_servers = df[(df['timestamp'] == ts) & (df['status'] == 'ONLINE')]['server_id'].nunique()
        if online_servers >= 2:
            valid_timestamps.append(ts)
    
    if not valid_timestamps:
        print("Warning: No periods found with at least 2 servers online simultaneously")
        return {}
    
    # Filter dataframe to only include these timestamps
    valid_df = df[df['timestamp'].isin(valid_timestamps)]
    
    # Set x-axis range
    x_min = min(valid_timestamps)
    x_max = max(valid_timestamps)
    
    # Create a figure with multiple subplots
    fig, axs = plt.subplots(5, 1, figsize=(12, 15), sharex=True)
    
    # Get unique servers
    servers = df['server_id'].unique()
    colors = ['blue', 'green', 'red', 'purple', 'orange']
    
    # Plot CPU utilization
    for i, server in enumerate(servers):
        server_data = df[df['server_id'] == server]
        axs[0].plot(server_data['timestamp'], server_data['cpu'], 
                   label=server, color=colors[i % len(colors)])
    axs[0].set_ylabel('CPU Utilization (%)')
    axs[0].set_title('CPU Utilization Over Time')
    axs[0].legend()
    axs[0].grid(True)
    axs[0].set_xlim(x_min, x_max)
    
    # Plot Memory usage
    for i, server in enumerate(servers):
        server_data = df[df['server_id'] == server]
        axs[1].plot(server_data['timestamp'], server_data['memory'], 
                   label=server, color=colors[i % len(colors)])
    axs[1].set_ylabel('Memory Usage (%)')
    axs[1].set_title('Memory Usage Over Time')
    axs[1].legend()
    axs[1].grid(True)
    axs[1].set_xlim(x_min, x_max)
    
    # Plot Queue size
    for i, server in enumerate(servers):
        server_data = df[df['server_id'] == server]
        axs[2].plot(server_data['timestamp'], server_data['queue'], 
                   label=server, color=colors[i % len(colors)])
    axs[2].set_ylabel('Queue Size')
    axs[2].set_title('Queue Size Over Time')
    axs[2].legend()
    axs[2].grid(True)
    axs[2].set_xlim(x_min, x_max)
    
    # Plot Latency
    for i, server in enumerate(servers):
        server_data = df[df['server_id'] == server]
        axs[3].plot(server_data['timestamp'], server_data['latency'], 
                   label=server, color=colors[i % len(colors)])
    axs[3].set_ylabel('Latency (ms)')
    axs[3].set_title('Network Latency Over Time')
    axs[3].legend()
    axs[3].grid(True)
    axs[3].set_xlim(x_min, x_max)
    
    # Plot Rank
    for i, server in enumerate(servers):
        server_data = df[df['server_id'] == server]
        axs[4].plot(server_data['timestamp'], server_data['rank'], 
                   label=server, color=colors[i % len(colors)])
    axs[4].set_ylabel('Server Rank')
    axs[4].set_title('Server Rank Over Time')
    axs[4].legend()
    axs[4].grid(True)
    axs[4].set_xlim(x_min, x_max)
    
    # Add annotation showing time when work stealing occurs
    for ax in axs:
        ax.axvspan(x_min, x_max, alpha=0.2, color='yellow', label='Multiple Servers Online')
    
    # Format x-axis
    fig.autofmt_xdate()
    plt.xlabel('Time')
    plt.tight_layout()
    
    # Save figure
    plt.savefig('server_metrics_analysis.png', dpi=300)
    plt.close()
    
    # Calculate statistics only for the valid timeframe
    stats = {}
    for server in servers:
        server_data = valid_df[valid_df['server_id'] == server]
        if not server_data.empty:
            stats[server] = {
                'avg_cpu': server_data['cpu'].mean(),
                'max_cpu': server_data['cpu'].max(),
                'avg_memory': server_data['memory'].mean(),
                'max_memory': server_data['memory'].max(),
                'avg_queue': server_data['queue'].mean(),
                'max_queue': server_data['queue'].max(),
                'avg_latency': server_data['latency'].mean(),
                'max_latency': server_data['latency'].max(),
                'avg_rank': server_data['rank'].mean(),
                'max_rank': server_data['rank'].max(),
                'online_ratio': len(server_data[server_data['status'] == 'ONLINE']) / len(server_data) if len(server_data) > 0 else 0
            }
    
    return stats

def main():
    # Parse metrics log
    df = parse_metrics_log('logs/metrics_monitor.log')
    
    # Analyze and plot data
    stats = analyze_and_plot(df)
    
    # Print statistics
    print("Server Statistics Summary:")
    for server, stat in stats.items():
        print(f"\n{server}:")
        print(f"  Average CPU: {stat['avg_cpu']:.2f}%")
        print(f"  Maximum CPU: {stat['max_cpu']:.2f}%")
        print(f"  Average Memory: {stat['avg_memory']:.2f}%")
        print(f"  Maximum Memory: {stat['max_memory']:.2f}%")
        print(f"  Average Queue: {stat['avg_queue']:.2f}")
        print(f"  Maximum Queue: {stat['max_queue']:.2f}")
        print(f"  Average Latency: {stat['avg_latency']:.2f}ms")
        print(f"  Maximum Latency: {stat['max_latency']:.2f}ms")
        print(f"  Average Rank: {stat['avg_rank']:.2f}")
        print(f"  Maximum Rank: {stat['max_rank']:.2f}")
        print(f"  Uptime Percentage: {stat['online_ratio']*100:.2f}%")

if __name__ == "__main__":
    main()