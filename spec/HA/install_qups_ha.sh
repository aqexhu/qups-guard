#!/usr/bin/env bash

set -e

if [ "$EUID" -ne 0 ]; then
  echo "Error: Please run this script with sudo or as root."
  exit 1
fi

REAL_USER=${SUDO_USER:-$USER}
if [ "$REAL_USER" = "root" ]; then
    read -p "[ACTION REQUIRED] Enter username: " TARGET_USER
else
    TARGET_USER=$REAL_USER
fi

USER_HOME="/home/$TARGET_USER"

if [ ! -d "$USER_HOME" ]; then
    echo "Error: User home directory $USER_HOME does not exist."
    exit 1
fi

read -p "[ACTION REQUIRED] Run apt upgrade? [y/N]: " RUN_UPGRADE
read -p "[ACTION REQUIRED] Install Mosquitto MQTT Broker? [Y/n]: " INSTALL_MOSQUITTO
INSTALL_MOSQUITTO=${INSTALL_MOSQUITTO:-Y}

read -p "[ACTION REQUIRED] Install Home Assistant Container? [Y/n]: " INSTALL_HA
INSTALL_HA=${INSTALL_HA:-Y}

DEFAULT_REPO="https://github.com/aqexhu/qups-guard.git"
read -p "[ACTION REQUIRED] Enter local path to qups-guard (leave blank to clone $DEFAULT_REPO): " LOCAL_QUPS_PATH

if [[ "$INSTALL_HA" =~ ^[Yy]$ ]]; then
    read -p "[ACTION REQUIRED] Enter Timezone [Europe/Budapest]: " TIMEZONE
    TIMEZONE=${TIMEZONE:-Europe/Budapest}
fi

read -p "[ACTION REQUIRED] Enter MQTT Broker Host [127.0.0.1]: " MQTT_BROKER
MQTT_BROKER=${MQTT_BROKER:-127.0.0.1}

read -p "[ACTION REQUIRED] Enter MQTT Username [qups_user]: " MQTT_USER
MQTT_USER=${MQTT_USER:-qups_user}

read -sp "[ACTION REQUIRED] Enter MQTT Password: " MQTT_PASS
echo ""
if [ -z "$MQTT_PASS" ]; then
    echo "Error: MQTT password cannot be empty."
    exit 1
fi

read -p "[ACTION REQUIRED] Enter DIP switch code [10]: " DIP_CODE
DIP_CODE=${DIP_CODE:-10}

apt update
if [[ "$RUN_UPGRADE" =~ ^[Yy]$ ]]; then
    apt upgrade -y
fi

# Base build dependencies
PKGS="build-essential git libgpiod-dev libmosquitto-dev libcjson-dev"

if [[ "$INSTALL_MOSQUITTO" =~ ^[Yy]$ ]]; then
    PKGS="$PKGS mosquitto mosquitto-clients"
fi

apt install -y $PKGS

# Mosquitto Setup
if [[ "$INSTALL_MOSQUITTO" =~ ^[Yy]$ ]]; then
    systemctl enable mosquitto
    mosquitto_passwd -c -b /etc/mosquitto/passwd "$MQTT_USER" "$MQTT_PASS"
    chown mosquitto:mosquitto /etc/mosquitto/passwd
    chmod 600 /etc/mosquitto/passwd

    cat <<EOF > /etc/mosquitto/conf.d/default.conf
listener 1883
allow_anonymous false
password_file /etc/mosquitto/passwd
EOF

    chown mosquitto:mosquitto /etc/mosquitto/conf.d/default.conf
    systemctl restart mosquitto
fi

# Home Assistant Setup
if [[ "$INSTALL_HA" =~ ^[Yy]$ ]]; then
    if ! command -v docker &> /dev/null; then
        curl -fsSL https://get.docker.com | sh
        usermod -aG docker "$TARGET_USER"
    fi

    mkdir -p "$USER_HOME/homeassistant/config"
    chown -R "$TARGET_USER:$TARGET_USER" "$USER_HOME/homeassistant"

    if [ "$(docker ps -aq -f name=homeassistant)" ]; then
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
fi

# qups-guard2-ha Setup
QUPS_DIR="$USER_HOME/qups-guard"

if [ -z "$LOCAL_QUPS_PATH" ]; then
    mkdir -p "$QUPS_DIR"
    git clone "$DEFAULT_REPO" "$QUPS_DIR"
else
    if [ -f "$LOCAL_QUPS_PATH" ]; then
        mkdir -p "$QUPS_DIR"
        cp "$LOCAL_QUPS_PATH" "$QUPS_DIR/qups-guard2-ha.c"
    elif [ -d "$LOCAL_QUPS_PATH" ]; then
        mkdir -p "$QUPS_DIR"
        cp -r "$LOCAL_QUPS_PATH"/* "$QUPS_DIR/"
    else
        echo "Error: Path $LOCAL_QUPS_PATH does not exist."
        exit 1
    fi
fi

if [ ! -f "$QUPS_DIR/qups-guard2-ha.c" ]; then
    echo "Error: qups-guard2-ha.c not found in $QUPS_DIR."
    exit 1
fi

cd "$QUPS_DIR"
gcc -O2 qups-guard2-ha.c -lgpiod -lmosquitto -lcjson -lpthread -o qups-guard2-ha
mv qups-guard2-ha /usr/local/bin/
chmod +x /usr/local/bin/qups-guard2-ha

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
    "broker": "$MQTT_BROKER",
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

echo "Installation complete."
if [[ "$INSTALL_HA" =~ ^[Yy]$ ]]; then
    echo "Home Assistant: http://$(hostname -I | awk '{print $1}'):8123"
fi

read -p "[ACTION REQUIRED] Do you want to monitor the service logs now? [y/N]: " SHOW_LOGS
if [[ "$SHOW_LOGS" =~ ^[Yy]$ ]]; then
    journalctl -u qups-guard2-ha -f
fi
