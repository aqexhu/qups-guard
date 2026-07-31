#!/usr/bin/env bash
#
# Automated Installer for Home Assistant, Mosquitto, and qups-guard2-ha
# Target System: Debian Trixie on Raspberry Pi 5
#

set -e

# Visual formatting
RED='\030[0;31m'
GREEN='\032[0;32m'
YELLOW='\033[1;33m'
NC='\030[0m' # No Color

echo -e "${GREEN}====================================================${NC}"
echo -e "${GREEN}  qUPS Guard & Home Assistant Automated Installer   ${NC}"
echo -e "${GREEN}====================================================${NC}"

# Ensure script is run with sudo
if [ "$EUID" -ne 0 ]; then
  echo -e "${RED}Error: Please run this script with sudo or as root.${NC}"
  exit 1
fi

# Detect non-root user for home directory paths
REAL_USER=${SUDO_USER:-$USER}
if [ "$REAL_USER" = "root" ]; then
    read -p "Enter the standard non-root username (e.g. pi or john): " TARGET_USER
else
    TARGET_USER=$REAL_USER
fi

USER_HOME="/home/$TARGET_USER"

if [ ! -d "$USER_HOME" ]; then
    echo -e "${RED}Error: User home directory $USER_HOME does not exist.${NC}"
    exit 1
fi

echo -e "${YELLOW}Gathering configuration details...${NC}"

# Prompt for user options with sensible defaults
read -p "Enter System Timezone [Europe/Budapest]: " TIMEZONE
TIMEZONE=${TIMEZONE:-Europe/Budapest}

read -p "Enter MQTT Username [qups_user]: " MQTT_USER
MQTT_USER=${MQTT_USER:-qups_user}

read -sp "Enter MQTT Password: " MQTT_PASS
echo ""
if [ -z "$MQTT_PASS" ]; then
    echo -e "${RED}Error: MQTT password cannot be empty.${NC}"
    exit 1
fi

read -p "Enter DIP switch configuration code (e.g. 10, 01, 11) [10]: " DIP_CODE
DIP_CODE=${DIP_CODE:-10}

read -p "Enter Git Repository URL (leave blank if qups-guard2-ha.c is already in local directory): " REPO_URL

echo -e "\n${GREEN}Step 1: Updating system & installing dependencies...${NC}"
apt update && apt upgrade -y
apt install -y build-essential git libgpiod-dev libmosquitto-dev libcjson-dev mosquitto mosquitto-clients

echo -e "\n${GREEN}Step 2: Installing Docker Engine...${NC}"
if ! command -v docker &> /dev/null; then
    curl -fsSL https://get.docker.com | sh
    usermod -aG docker "$TARGET_USER"
else
    echo "Docker is already installed."
fi

echo -e "\n${GREEN}Step 3: Configuring Mosquitto MQTT Broker...${NC}"
systemctl enable mosquitto
mosquitto_passwd -c -b /etc/mosquitto/passwd "$MQTT_USER" "$MQTT_PASS"

cat <<EOF > /etc/mosquitto/conf.d/default.conf
listener 1883
allow_anonymous false
password_file /etc/mosquitto/passwd
EOF

systemctl restart mosquitto

echo -e "\n${GREEN}Step 4: Launching Home Assistant Docker Container...${NC}"
mkdir -p "$USER_HOME/homeassistant/config"
chown -R "$TARGET_USER:$TARGET_USER" "$USER_HOME/homeassistant"

if [ "$(docker ps -aq -f name=homeassistant)" ]; then
    echo "Removing existing Home Assistant container..."
    docker rm -f homeassistant
fi

docker run -d \
  --name homeassistant \
  --privileged \
  --restart=unless-stopped \
  -e TZ="$TIMEZONE" \
  -v "$USER_HOME/homeassistant/config:/config" \
  --network=host \
  ghcr.io/home-assistant/home-assistant:stable

echo -e "\n${GREEN}Step 5: Setting up qups-guard2-ha...${NC}"
QUPS_DIR="$USER_HOME/qups-guard"
mkdir -p "$QUPS_DIR"

if [ -n "$REPO_URL" ]; then
    echo "Cloning source code from $REPO_URL..."
    git clone "$REPO_URL" "$QUPS_DIR"
fi

if [ ! -f "$QUPS_DIR/qups-guard2-ha.c" ]; then
    echo -e "${RED}Error: qups-guard2-ha.c not found in $QUPS_DIR.${NC}"
    echo "Please copy qups-guard2-ha.c to $QUPS_DIR and rerun the script."
    exit 1
fi

cd "$QUPS_DIR"
echo "Compiling qups-guard2-ha..."
gcc -O2 qups-guard2-ha.c -lgpiod -lmosquitto -lcjson -lpthread -o qups-guard2-ha
mv qups-guard2-ha /usr/local/bin/
chmod +x /usr/local/bin/qups-guard2-ha

# Create JSON config file
cat <<EOF > "$QUPS_DIR/qups-guard2-ha.json"
{
  "gpio": {
    "dip": "$DIP_CODE",
    "chip_path": "/dev/gpiochip0"
  },
  "ups": {
    "shutdown_delay": 10
  },
  "mqtt": {
    "enabled": true,
    "broker": "127.0.0.1",
    "port": 1883,
    "username": "$MQTT_USER",
    "password": "$MQTT_PASS",
    "node_id": "qups_guard",
    "state_topic": "qups/state",
    "discovery_prefix": "homeassistant"
  }
}
EOF

chown -R "$TARGET_USER:$TARGET_USER" "$QUPS_DIR"

echo -e "\n${GREEN}Step 6: Creating systemd service...${NC}"
cat <<EOF > /etc/systemd/system/qups-guard2-ha.service
[Unit]
Description=qUPS Guard HA Daemon
After=network.target mosquitto.service

[Service]
Type=simple
ExecStart=/usr/local/bin/qups-guard2-ha --config $QUPS_DIR/qups-guard2-ha.json
Restart=always
RestartSec=5
User=root

[Install]
WantedBy=multi-user.target
EOF

systemctl daemon-reload
systemctl enable qups-guard2-ha
systemctl restart qups-guard2-ha

echo -e "\n${GREEN}====================================================${NC}"
echo -e "${GREEN}             Installation Complete!                 ${NC}"
echo -e "${GREEN}====================================================${NC}"
echo -e "1. Access Home Assistant at: ${YELLOW}http://$(hostname -I | awk '{print $1}'):8123${NC}"
echo -e "2. During HA onboarding, add the ${YELLOW}MQTT Integration${NC} using:"
echo -e "   - Broker:   ${YELLOW}127.0.0.1${NC}"
echo -e "   - Port:     ${YELLOW}1883${NC}"
echo -e "   - Username: ${YELLOW}$MQTT_USER${NC}"
echo -e "   - Password: ${YELLOW}(the password you provided)${NC}"
echo -e "3. Check service status using: ${YELLOW}sudo journalctl -u qups-guard2-ha -f${NC}"
