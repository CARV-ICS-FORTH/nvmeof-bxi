#!/bin/bash

# Default inline data size (disabled)
INLINE_DATA_SIZE=0

# Check if IP address parameter is provided
if [ -z "$1" ]; then
    echo "Usage: $0 <IP_ADDRESS> [num_volumes] [-inline]"
    echo "Example: $0 192.168.1.100"
    echo "Example: $0 192.168.1.100 3"
    echo "Example: $0 192.168.1.100 3 -inline"
    exit 1
fi

IP_ADDRESS=$1
NUM_VOLUMES=${2:-1}  # Default to 1 volume if not specified

# Check for -inline option (can be 3rd or 2nd parameter)
if [ "$3" == "-inline" ] || [ "$2" == "-inline" ]; then
    INLINE_DATA_SIZE=4096
    # If -inline is the 2nd parameter, reset NUM_VOLUMES to 1
    if [ "$2" == "-inline" ]; then
        NUM_VOLUMES=1
    fi
fi

echo "Creating $NUM_VOLUMES volumes on $IP_ADDRESS"
if [ $INLINE_DATA_SIZE -gt 0 ]; then
    echo "Inline data enabled (size: $INLINE_DATA_SIZE)"
fi

# Create multiple AIO bdevs
for i in $(seq 0 $((NUM_VOLUMES-1))); do
    echo "Creating AIO bdev: AIO$i"
    ./scripts/rpc.py bdev_aio_create /tmp/gesalous_junk"$i".dat AIO"$i" 4096
done

# Create the RDMA transport (only once)
echo "Creating RDMA transport"
./scripts/rpc.py nvmf_create_transport -t RDMA \
    -u 8192 \
    -m 4 \
    -c $INLINE_DATA_SIZE

# Create multiple NVMe-oF subsystems
for i in $(seq 1 "$NUM_VOLUMES"); do
    SUBSYSTEM_NAME="nqn.2016-06.io.spdk:cnode$i"
    SERIAL_NUMBER="SPDK$(printf "%011d" "$i")"
    CONTROLLER_NAME="SPDK_Controller$i"
    AIO_INDEX=$((i-1))  # AIO devices start from 0, cnode starts from 1
    
    echo "Creating subsystem: $SUBSYSTEM_NAME"
    
    # Create NVMe-oF subsystem
    ./scripts/rpc.py nvmf_create_subsystem "$SUBSYSTEM_NAME" \
        -a \
        -s "$SERIAL_NUMBER" \
        -d "$CONTROLLER_NAME"

    # Add namespace to subsystem
    ./scripts/rpc.py nvmf_subsystem_add_ns "$SUBSYSTEM_NAME" AIO"$AIO_INDEX"

    # Add listener to subsystem
    ./scripts/rpc.py nvmf_subsystem_add_listener "$SUBSYSTEM_NAME" \
        -t rdma \
        -a "$IP_ADDRESS" \
        -s 4420
        
    echo "Volume $i created successfully"
done

echo "All $NUM_VOLUMES volumes created successfully!"
