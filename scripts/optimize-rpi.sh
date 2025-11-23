#!/usr/bin/env bash
# Raspberry Pi Performance Tuning Script for ThemisDB
# Automatically applies recommended system optimizations

set -euo pipefail

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

# Check if running as root
if [ "$EUID" -eq 0 ]; then
   echo -e "${RED}Do not run this script as root. Use sudo when needed.${NC}"
   exit 1
fi

echo -e "${BLUE}=== Raspberry Pi Performance Tuning for ThemisDB ===${NC}"
echo ""

# Detect Raspberry Pi model
detect_rpi_model() {
    if [ -f /proc/device-tree/model ]; then
        MODEL=$(cat /proc/device-tree/model)
        echo -e "${GREEN}Detected: $MODEL${NC}"
        
        if echo "$MODEL" | grep -q "Raspberry Pi 5"; then
            RPI_MODEL="rpi5"
            CONFIG_FILE="config.rpi5.json"
        elif echo "$MODEL" | grep -q "Raspberry Pi 4"; then
            RPI_MODEL="rpi4"
            CONFIG_FILE="config.rpi4.json"
        elif echo "$MODEL" | grep -q "Raspberry Pi 3"; then
            RPI_MODEL="rpi3"
            CONFIG_FILE="config.rpi3.json"
        else
            RPI_MODEL="unknown"
            CONFIG_FILE="config.json"
            echo -e "${YELLOW}Unknown Raspberry Pi model, using default config${NC}"
        fi
    else
        echo -e "${YELLOW}Not a Raspberry Pi or model detection failed${NC}"
        RPI_MODEL="unknown"
        CONFIG_FILE="config.json"
    fi
}

# Check system resources
check_system() {
    echo -e "${BLUE}System Information:${NC}"
    echo "CPU: $(nproc) cores"
    echo "Memory: $(free -h | grep Mem | awk '{print $2}')"
    echo "Swap: $(free -h | grep Swap | awk '{print $2}')"
    
    if command -v vcgencmd &> /dev/null; then
        echo "Temperature: $(vcgencmd measure_temp)"
        THROTTLE=$(vcgencmd get_throttled)
        if [ "$THROTTLE" = "throttled=0x0" ]; then
            echo -e "${GREEN}Throttling: None${NC}"
        else
            echo -e "${YELLOW}Throttling detected: $THROTTLE${NC}"
        fi
    fi
    echo ""
}

# CPU Governor
setup_cpu_governor() {
    echo -e "${BLUE}Configuring CPU Governor...${NC}"
    
    CURRENT_GOV=$(cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor 2>/dev/null || echo "unknown")
    echo "Current governor: $CURRENT_GOV"
    
    if [ "$CURRENT_GOV" != "performance" ]; then
        echo "Setting to performance mode..."
        echo performance | sudo tee /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor > /dev/null
        echo -e "${GREEN}✓ CPU governor set to performance${NC}"
        
        # Make permanent
        if ! grep -q "scaling_governor" /etc/rc.local 2>/dev/null; then
            sudo sed -i '/^exit 0/i echo performance | tee /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor' /etc/rc.local 2>/dev/null || true
            echo -e "${GREEN}✓ Made permanent in /etc/rc.local${NC}"
        fi
    else
        echo -e "${GREEN}✓ Already set to performance${NC}"
    fi
    echo ""
}

# Disable Transparent Huge Pages
disable_thp() {
    echo -e "${BLUE}Configuring Transparent Huge Pages...${NC}"
    
    if [ -f /sys/kernel/mm/transparent_hugepage/enabled ]; then
        CURRENT_THP=$(cat /sys/kernel/mm/transparent_hugepage/enabled | grep -o '\[.*\]' | tr -d '[]')
        echo "Current THP: $CURRENT_THP"
        
        if [ "$CURRENT_THP" != "never" ]; then
            echo "Disabling THP..."
            echo never | sudo tee /sys/kernel/mm/transparent_hugepage/enabled > /dev/null
            echo never | sudo tee /sys/kernel/mm/transparent_hugepage/defrag > /dev/null
            echo -e "${GREEN}✓ Transparent Huge Pages disabled${NC}"
            
            # Make permanent
            if ! grep -q "transparent_hugepage" /etc/rc.local 2>/dev/null; then
                sudo sed -i '/^exit 0/i echo never > /sys/kernel/mm/transparent_hugepage/enabled\necho never > /sys/kernel/mm/transparent_hugepage/defrag' /etc/rc.local 2>/dev/null || true
                echo -e "${GREEN}✓ Made permanent in /etc/rc.local${NC}"
            fi
        else
            echo -e "${GREEN}✓ Already disabled${NC}"
        fi
    else
        echo -e "${YELLOW}⚠ THP not available on this system${NC}"
    fi
    echo ""
}

# Configure swap
configure_swap() {
    echo -e "${BLUE}Configuring Swap...${NC}"
    
    TOTAL_MEM=$(free -m | grep Mem | awk '{print $2}')
    CURRENT_SWAPPINESS=$(cat /proc/sys/vm/swappiness)
    
    echo "Total Memory: ${TOTAL_MEM}MB"
    echo "Current swappiness: $CURRENT_SWAPPINESS"
    
    # Adjust swappiness based on memory
    if [ "$TOTAL_MEM" -gt 6000 ]; then
        TARGET_SWAPPINESS=1
        echo "High memory system, setting swappiness to 1"
    elif [ "$TOTAL_MEM" -gt 3000 ]; then
        TARGET_SWAPPINESS=10
        echo "Medium memory system, setting swappiness to 10"
    else
        TARGET_SWAPPINESS=30
        echo "Low memory system, setting swappiness to 30"
    fi
    
    if [ "$CURRENT_SWAPPINESS" -ne "$TARGET_SWAPPINESS" ]; then
        sudo sysctl vm.swappiness=$TARGET_SWAPPINESS
        
        if ! grep -q "vm.swappiness" /etc/sysctl.conf; then
            echo "vm.swappiness=$TARGET_SWAPPINESS" | sudo tee -a /etc/sysctl.conf > /dev/null
            echo -e "${GREEN}✓ Swappiness configured${NC}"
        fi
    else
        echo -e "${GREEN}✓ Swappiness already optimal${NC}"
    fi
    echo ""
}

# Network tuning
tune_network() {
    echo -e "${BLUE}Tuning Network Settings...${NC}"
    
    sudo tee /etc/sysctl.d/99-themisdb-network.conf > /dev/null << 'EOF'
# ThemisDB Network Optimizations
net.core.rmem_max=16777216
net.core.wmem_max=16777216
net.ipv4.tcp_rmem=4096 87380 16777216
net.ipv4.tcp_wmem=4096 65536 16777216
net.core.netdev_max_backlog=5000
net.ipv4.tcp_max_syn_backlog=3240
net.ipv4.tcp_fin_timeout=30
EOF
    
    sudo sysctl -p /etc/sysctl.d/99-themisdb-network.conf > /dev/null 2>&1
    echo -e "${GREEN}✓ Network settings optimized${NC}"
    echo ""
}

# Configure ThemisDB
configure_themisdb() {
    echo -e "${BLUE}Configuring ThemisDB...${NC}"
    
    if [ "$RPI_MODEL" != "unknown" ] && [ -f "config/$CONFIG_FILE" ]; then
        echo "Using optimized config: $CONFIG_FILE"
        
        if [ -f "/etc/themisdb/config.json" ]; then
            sudo cp /etc/themisdb/config.json /etc/themisdb/config.json.backup
            echo "Backed up existing config to config.json.backup"
        fi
        
        sudo cp "config/$CONFIG_FILE" /etc/themisdb/config.json
        sudo chown themisdb:themisdb /etc/themisdb/config.json
        echo -e "${GREEN}✓ ThemisDB configured for $RPI_MODEL${NC}"
    else
        echo -e "${YELLOW}⚠ Model-specific config not found, skipping${NC}"
    fi
    echo ""
}

# Setup systemd service limits
setup_service_limits() {
    echo -e "${BLUE}Configuring Service Limits...${NC}"
    
    sudo mkdir -p /etc/systemd/system/themisdb.service.d/
    
    sudo tee /etc/systemd/system/themisdb.service.d/limits.conf > /dev/null << 'EOF'
[Service]
# File descriptor limits
LimitNOFILE=65536

# OOM protection
OOMScoreAdjust=-500

# CPU affinity (all cores)
CPUAffinity=0 1 2 3
EOF
    
    echo -e "${GREEN}✓ Service limits configured${NC}"
    echo ""
}

# Check for SSD
check_storage() {
    echo -e "${BLUE}Storage Check...${NC}"
    
    # Check if data is on SD card or SSD
    DATA_PATH="/var/lib/themisdb"
    if [ -d "$DATA_PATH" ]; then
        MOUNT_POINT=$(df -P "$DATA_PATH" | tail -1 | awk '{print $1}')
        echo "Data path: $DATA_PATH"
        echo "Mount point: $MOUNT_POINT"
        
        if echo "$MOUNT_POINT" | grep -q "mmcblk"; then
            echo -e "${YELLOW}⚠ WARNING: Data is on SD card${NC}"
            echo -e "${YELLOW}  Recommendation: Use SSD via USB 3.0 for better performance${NC}"
            echo -e "${YELLOW}  See: docs/RASPBERRY_PI_TUNING.md#storage-optimizations${NC}"
        else
            echo -e "${GREEN}✓ Data appears to be on external storage${NC}"
        fi
    else
        echo -e "${YELLOW}⚠ Data directory not found (not installed?)${NC}"
    fi
    echo ""
}

# Apply all optimizations
apply_all() {
    detect_rpi_model
    check_system
    setup_cpu_governor
    disable_thp
    configure_swap
    tune_network
    setup_service_limits
    configure_themisdb
    check_storage
}

# Main menu
show_menu() {
    echo -e "${BLUE}Select optimization to apply:${NC}"
    echo "1) Apply all optimizations (recommended)"
    echo "2) CPU Governor only"
    echo "3) Disable THP only"
    echo "4) Configure swap only"
    echo "5) Network tuning only"
    echo "6) ThemisDB config only"
    echo "7) Service limits only"
    echo "8) Check system info"
    echo "9) Exit"
    echo ""
    read -p "Choice [1-9]: " choice
    
    case $choice in
        1) apply_all ;;
        2) setup_cpu_governor ;;
        3) disable_thp ;;
        4) configure_swap ;;
        5) tune_network ;;
        6) detect_rpi_model; configure_themisdb ;;
        7) setup_service_limits ;;
        8) detect_rpi_model; check_system ;;
        9) exit 0 ;;
        *) echo -e "${RED}Invalid choice${NC}"; show_menu ;;
    esac
}

# Main execution
if [ $# -eq 0 ]; then
    show_menu
else
    case "$1" in
        --all)
            apply_all
            ;;
        --help)
            echo "Usage: $0 [--all|--help]"
            echo ""
            echo "Options:"
            echo "  --all     Apply all optimizations automatically"
            echo "  --help    Show this help message"
            echo ""
            echo "Run without arguments for interactive menu"
            exit 0
            ;;
        *)
            echo -e "${RED}Unknown option: $1${NC}"
            echo "Use --help for usage information"
            exit 1
            ;;
    esac
fi

# Final message
echo -e "${GREEN}=========================================${NC}"
echo -e "${GREEN}Optimization complete!${NC}"
echo -e "${GREEN}=========================================${NC}"
echo ""
echo "Recommended next steps:"
echo "1. Reload systemd: sudo systemctl daemon-reload"
echo "2. Restart ThemisDB: sudo systemctl restart themisdb"
echo "3. Monitor temperature: watch -n 1 vcgencmd measure_temp"
echo "4. Run benchmarks: ./scripts/run-arm-benchmarks.sh"
echo ""
echo "For more tuning options, see: docs/RASPBERRY_PI_TUNING.md"

exit 0
