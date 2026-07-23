#include <gpiod.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdint.h>
#include <pthread.h>
#include <string.h>
#include <syslog.h>
#include <errno.h>
#include <time.h>
#include <stdbool.h>
#include <mosquitto.h>
#include <cjson/cJSON.h>

// ==========================================
// GLOBALS & DEFAULT CONFIGURATION
// ==========================================
pthread_t g_thread, g_shdthread;

struct gpiod_chip *chip = NULL;
uint8_t lastval_pfo = 255, lastval_lim = 255;
struct timespec ts;
struct timespec start_time, test_time;
struct gpiod_line_request *in_request = NULL;
struct gpiod_line_request *shd_request = NULL;
struct gpiod_edge_event_buffer *evbuf = NULL;

#define CONSUMER "qUPS-guard"
#define POLLINTERVAL 1000
#define SHUTDOWN_DELAY 0

// State variable holding the suppression status of LOW UPS messages
// 0 = Active/Ready to log; 1 = Suppressed (already logged)
static uint8_t low_ups_suppressed = 0;

// Configurable settings
char chip_path[64] = "/dev/gpiochip0";
uint8_t shutdown_delay = SHUTDOWN_DELAY;
static uint8_t shutdown_pulse = 0;


struct DIPsw
{
    const char *DIP;
    unsigned int pfo_n;
    unsigned int lim_n;
    unsigned int shd_n;
} DIP_sw;

char dip_sw[4] = "";
bool dip_configured = false;

struct DIPsw DIPswa[10] = {
    {"10", 17, 27, 22}, {"01", 23, 24, 25}, {"11", 5, 6, 26}, 
    {"111", 4, 24, 23}, {"011", 14, 18, 15}, {"101", 25, 7, 8}, 
    {"001", 17, 22, 27}, {"110", 10, 11, 9}, {"010", 12, 20, 16}, 
    {"100", 19, 21, 26}
};

// MQTT Configuration
static struct mosquitto *g_mosq = NULL;
static bool mqtt_enabled = true;
static char mqtt_broker[128] = "127.0.0.1";
static int mqtt_port = 1883;
static char mqtt_user[64] = "";
static char mqtt_pass[64] = "";
static char node_id[64] = "qups_guard";
static char state_topic[128] = "qups/state";
static char discovery_prefix[64] = "homeassistant";

// ==========================================
// CONFIG FILE PARSER
// ==========================================
bool set_dip_switch(const char *code)
{
    unsigned int dip_len = strlen(code);
    if (dip_len == 2 || dip_len == 3)
    {
        strncpy(dip_sw, code, sizeof(dip_sw) - 1);
        dip_sw[sizeof(dip_sw) - 1] = '\0';
        for (unsigned int j = 0; j < 10; j++)
        {
            if (!strcmp(dip_sw, DIPswa[j].DIP))
            {
                DIP_sw = DIPswa[j];
                dip_configured = true;
                return true;
            }
        }
    }
    return false;
}

void load_config_file(const char *filepath)
{
    FILE *f = fopen(filepath, "rb");
    if (!f) {
        syslog(LOG_WARNING, "Config file %s not found. Using defaults/CLI flags.", filepath);
        return;
    }

    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);

    char *data = malloc(len + 1);
    if (!data) {
        fclose(f);
        return;
    }

    fread(data, 1, len, f);
    fclose(f);
    data[len] = '\0';

    cJSON *json = cJSON_Parse(data);
    free(data);

    if (!json) {
        syslog(LOG_ERR, "JSON Parse Error in config file: %s", cJSON_GetErrorPtr());
        return;
    }

    // Parse GPIO Config
    cJSON *gpio = cJSON_GetObjectItemCaseSensitive(json, "gpio");
    if (gpio) {
        cJSON *dip = cJSON_GetObjectItemCaseSensitive(gpio, "dip");
        if (cJSON_IsString(dip) && (dip->valuestring != NULL)) {
            set_dip_switch(dip->valuestring);
        }
        cJSON *cpath = cJSON_GetObjectItemCaseSensitive(gpio, "chip_path");
        if (cJSON_IsString(cpath) && (cpath->valuestring != NULL)) {
            strncpy(chip_path, cpath->valuestring, sizeof(chip_path) - 1);
        }
    }

    // Parse UPS Settings
    cJSON *ups = cJSON_GetObjectItemCaseSensitive(json, "ups");
    if (ups) {
        cJSON *sdelay = cJSON_GetObjectItemCaseSensitive(ups, "shutdown_delay");
        if (cJSON_IsNumber(sdelay)) {
            shutdown_delay = (uint8_t)sdelay->valueint;
        }
    }

    // Parse MQTT Settings
    cJSON *mqtt = cJSON_GetObjectItemCaseSensitive(json, "mqtt");
    if (mqtt) {
        cJSON *en = cJSON_GetObjectItemCaseSensitive(mqtt, "enabled");
        if (cJSON_IsBool(en)) {
            mqtt_enabled = cJSON_IsTrue(en);
        }
        cJSON *broker = cJSON_GetObjectItemCaseSensitive(mqtt, "broker");
        if (cJSON_IsString(broker) && (broker->valuestring != NULL)) {
            strncpy(mqtt_broker, broker->valuestring, sizeof(mqtt_broker) - 1);
        }
        cJSON *port = cJSON_GetObjectItemCaseSensitive(mqtt, "port");
        if (cJSON_IsNumber(port)) {
            mqtt_port = port->valueint;
        }
        cJSON *user = cJSON_GetObjectItemCaseSensitive(mqtt, "username");
        if (cJSON_IsString(user) && (user->valuestring != NULL)) {
            strncpy(mqtt_user, user->valuestring, sizeof(mqtt_user) - 1);
        }
        cJSON *pass = cJSON_GetObjectItemCaseSensitive(mqtt, "password");
        if (cJSON_IsString(pass) && (pass->valuestring != NULL)) {
            strncpy(mqtt_pass, pass->valuestring, sizeof(mqtt_pass) - 1);
        }
        cJSON *nid = cJSON_GetObjectItemCaseSensitive(mqtt, "node_id");
        if (cJSON_IsString(nid) && (nid->valuestring != NULL)) {
            strncpy(node_id, nid->valuestring, sizeof(node_id) - 1);
        }
        cJSON *stopic = cJSON_GetObjectItemCaseSensitive(mqtt, "state_topic");
        if (cJSON_IsString(stopic) && (stopic->valuestring != NULL)) {
            strncpy(state_topic, stopic->valuestring, sizeof(state_topic) - 1);
        }
        cJSON *dprefix = cJSON_GetObjectItemCaseSensitive(mqtt, "discovery_prefix");
        if (cJSON_IsString(dprefix) && (dprefix->valuestring != NULL)) {
            strncpy(discovery_prefix, dprefix->valuestring, sizeof(discovery_prefix) - 1);
        }
    }

    cJSON_Delete(json);
    syslog(LOG_INFO, "Config file successfully parsed: %s", filepath);
}

// ==========================================
// HELPER FUNCTIONS & MQTT
// ==========================================
double diffcltime(struct timespec a, struct timespec b)
{
    long long elapsed_nanoseconds = (b.tv_sec - a.tv_sec) * 1000000000LL +
                                    (b.tv_nsec - a.tv_nsec);

    return (double)(elapsed_nanoseconds / 1000.0);
}

void publish_mqtt_state(const char *mains, const char *bat_low, const char *status)
{
    if (!mqtt_enabled || !g_mosq) return;

    char payload[256];
    snprintf(payload, sizeof(payload),
             "{\"mains_power\":\"%s\",\"battery_low\":\"%s\",\"status\":\"%s\"}",
             mains, bat_low, status);

    int rc = mosquitto_publish(g_mosq, NULL, state_topic, strlen(payload), payload, 1, true);
    if (rc == MOSQ_ERR_SUCCESS) {
        syslog(LOG_INFO, "MQTT State Published -> %s", payload);
    } else {
        syslog(LOG_WARNING, "MQTT Publish failed: %s", mosquitto_strerror(rc));
    }
}

void publish_ha_discovery(void)
{
    if (!mqtt_enabled || !g_mosq) return;

    char topic[256];
    char payload[1024];

    const char *device_info =
        "\"device\":{"
            "\"identifiers\":[\"aqex_qups_guard\"],"
            "\"name\":\"AQEX qUPS Guard\","
            "\"manufacturer\":\"AQEX Electronics\","
            "\"model\":\"qUPS Series\""
        "}";

    // Mains Power Sensor
    snprintf(topic, sizeof(topic), "%s/binary_sensor/%s/mains_power/config", discovery_prefix, node_id);
    snprintf(payload, sizeof(payload),
             "{\"name\":\"qUPS Mains Power\","
              "\"unique_id\":\"%s_mains_power\","
              "\"state_topic\":\"%s\","
              "\"value_template\":\"{{ value_json.mains_power }}\","
              "\"payload_on\":\"ON\",\"payload_off\":\"OFF\","
              "\"device_class\":\"plug\",%s}",
             node_id, state_topic, device_info);
    mosquitto_publish(g_mosq, NULL, topic, strlen(payload), payload, 1, true);

    // Battery Low Sensor
    snprintf(topic, sizeof(topic), "%s/binary_sensor/%s/battery_low/config", discovery_prefix, node_id);
    snprintf(payload, sizeof(payload),
             "{\"name\":\"qUPS Energy Low\","
              "\"unique_id\":\"%s_battery_low\","
              "\"state_topic\":\"%s\","
              "\"value_template\":\"{{ value_json.battery_low }}\","
              "\"payload_on\":\"ON\",\"payload_off\":\"OFF\","
              "\"device_class\":\"problem\",%s}",
             node_id, state_topic, device_info);
    mosquitto_publish(g_mosq, NULL, topic, strlen(payload), payload, 1, true);

    // Status Sensor
    snprintf(topic, sizeof(topic), "%s/sensor/%s/status/config", discovery_prefix, node_id);
    snprintf(payload, sizeof(payload),
             "{\"name\":\"qUPS Status\","
              "\"unique_id\":\"%s_status\","
              "\"state_topic\":\"%s\","
              "\"value_template\":\"{{ value_json.status }}\","
              "\"icon\":\"mdi:uninterruptible-power-supply\",%s}",
             node_id, state_topic, device_info);
    mosquitto_publish(g_mosq, NULL, topic, strlen(payload), payload, 1, true);

    syslog(LOG_INFO, "Published HA MQTT Auto-Discovery topics.");
}

void mqtt_init(void)
{
    if (!mqtt_enabled) return;

    mosquitto_lib_init();
    g_mosq = mosquitto_new("qups-guard2-c", true, NULL);
    if (!g_mosq) {
        syslog(LOG_ERR, "Failed to create Mosquitto instance.");
        return;
    }

    if (strlen(mqtt_user) > 0 && strlen(mqtt_pass) > 0) {
        mosquitto_username_pw_set(g_mosq, mqtt_user, mqtt_pass);
    }

    mosquitto_reconnect_delay_set(g_mosq, 2, 30, true);

    if (mosquitto_connect(g_mosq, mqtt_broker, mqtt_port, 60) != MOSQ_ERR_SUCCESS) {
        syslog(LOG_WARNING, "Could not connect to MQTT broker immediately; reconnecting in background.");
    }

    mosquitto_loop_start(g_mosq);
    publish_ha_discovery();
}

// ==========================================
// WORKER THREADS
// ==========================================
void *g_shdcallback(void *args)
{
    while (true)
    {
        if (shutdown_pulse)
        {
            clock_gettime(CLOCK_MONOTONIC, &test_time);
            double dct = diffcltime(start_time, test_time);
            if (dct > POLLINTERVAL)
            {
                syslog(LOG_INFO, "Limit LOW since %.2f ms - initiating shutdown with delay %d.", dct/1000, shutdown_delay);

                publish_mqtt_state("OFF", "ON", "Shutting Down");
                sleep(1); 

                sleep(shutdown_delay);
                syslog(LOG_INFO, "Shutdown delay expired, shutting down system.");

                if (system("sudo shutdown -h now") == 0)
                {
                    syslog(LOG_INFO, "Shutdown sequence successfully initiated.");
                }
            }
        }
        fflush(stdout);
        usleep(POLLINTERVAL);
    }
}

void *g_callback(void *args)
{
    const int buf_capacity = 64;
    evbuf = gpiod_edge_event_buffer_new(buf_capacity);
    int64_t timeout_ns = (int64_t)ts.tv_sec * 1000000000LL + ts.tv_nsec;

    while (true)
    {
        int ret = gpiod_line_request_wait_edge_events(in_request, timeout_ns);
        if (ret == 1)
        {
            int nread = gpiod_line_request_read_edge_events(in_request, evbuf, buf_capacity);
            if (nread <= 0)
                continue;

            size_t nevents = gpiod_edge_event_buffer_get_num_events(evbuf);
            for (size_t i = 0; i < nevents; ++i)
            {
                struct gpiod_edge_event *ev = gpiod_edge_event_buffer_get_event(evbuf, i);
                enum gpiod_edge_event_type et = gpiod_edge_event_get_event_type(ev);
                unsigned int offset = gpiod_edge_event_get_line_offset(ev);

                if (offset == DIP_sw.pfo_n)
                {
                    uint8_t cur_val = (uint8_t)gpiod_line_request_get_value(in_request, DIP_sw.pfo_n);
                    if (et == GPIOD_EDGE_EVENT_FALLING_EDGE)
                    {
                        if (lastval_pfo != cur_val) {
                            syslog(LOG_INFO, "UPS line power NOK!");
                            publish_mqtt_state("OFF", lastval_lim == 0 ? "ON" : "OFF", "On Backup");
                        }
                    }
                    else if (et == GPIOD_EDGE_EVENT_RISING_EDGE)
                    {
                        if (lastval_pfo != cur_val) {
                            // Power restored: reset suppression state to allow logging on next outage
                            low_ups_suppressed = 0;
                            syslog(LOG_INFO, "UPS line power OK.");
                            publish_mqtt_state("ON", lastval_lim == 0 ? "ON" : "OFF", "Online");
                        }
                    }
                    lastval_pfo = cur_val;
                }
                else if (offset == DIP_sw.lim_n)
                {
                    uint8_t cur_val = (uint8_t)gpiod_line_request_get_value(in_request, DIP_sw.lim_n);
                    if (et == GPIOD_EDGE_EVENT_FALLING_EDGE)
                    {
                        shutdown_pulse = 1;
                        clock_gettime(CLOCK_MONOTONIC, &start_time);
			if (lastval_pfo == 0)
			{
				if (low_ups_suppressed == 0) 
				{
                        		syslog(LOG_INFO, "UPS energy level LOW.");
                        		publish_mqtt_state(lastval_pfo == 1 ? "ON" : "OFF", "ON", "Low Energy Warning");
				}
			}
			/// power is on, but energy store is below LOW - no need to log message
			//else
			//{
			//	syslog(LOG_INFO, "UPS energy level LOW.");
			//}
                    }
                    else if (et == GPIOD_EDGE_EVENT_RISING_EDGE)
                    {
                        syslog(LOG_INFO, "UPS energy level HIGH.");
                        shutdown_pulse = 0;
                        publish_mqtt_state(lastval_pfo == 1 ? "ON" : "OFF", "OFF", lastval_pfo == 1 ? "Online" : "On Backup");
                    }
                    lastval_lim = cur_val;
                }
            }
        }
        fflush(stdout);
        usleep(POLLINTERVAL);
    }
}

// ==========================================
// HARDWARE MANAGEMENT
// ==========================================
int g_gpiorelease(void)
{
    if (in_request)
        gpiod_line_request_release(in_request);
    if (shd_request)
        gpiod_line_request_release(shd_request);
    if (evbuf)
        gpiod_edge_event_buffer_free(evbuf);
    if (chip)
        gpiod_chip_close(chip);

    if (mqtt_enabled && g_mosq) {
        mosquitto_loop_stop(g_mosq, true);
        mosquitto_destroy(g_mosq);
        mosquitto_lib_cleanup();
    }
    return 0;
}

int g_gpioinit(void)
{
    chip = gpiod_chip_open(chip_path);
    if (!chip)
    {
        // Fallback checks
        chip = gpiod_chip_open("/dev/gpiochip0");
        if (!chip) {
            chip = gpiod_chip_open("/dev/gpiochip4");
            if (!chip) {
                syslog(LOG_ERR, "Open GPIO chip failed\n");
                exit(EXIT_FAILURE);
            }
        }
    }

    struct gpiod_chip_info *info = gpiod_chip_get_info(chip);
    if (info)
    {
        syslog(LOG_INFO, "Chip name: %s - label: %s - %zu lines\n",
               gpiod_chip_info_get_name(info), gpiod_chip_info_get_label(info), gpiod_chip_info_get_num_lines(info));
        gpiod_chip_info_free(info);
    }

    // Output line setup
    struct gpiod_request_config *reqcfg_out = gpiod_request_config_new();
    gpiod_request_config_set_consumer(reqcfg_out, CONSUMER);
    struct gpiod_line_settings *settings_out = gpiod_line_settings_new();
    gpiod_line_settings_set_direction(settings_out, GPIOD_LINE_DIRECTION_OUTPUT);
    gpiod_line_settings_set_output_value(settings_out, GPIOD_LINE_VALUE_ACTIVE);
    struct gpiod_line_config *linecfg_out = gpiod_line_config_new();
    unsigned int out_offsets[1] = {DIP_sw.shd_n};
    gpiod_line_config_add_line_settings(linecfg_out, out_offsets, 1, settings_out);
    shd_request = gpiod_chip_request_lines(chip, reqcfg_out, linecfg_out);
    gpiod_line_settings_free(settings_out);
    gpiod_line_config_free(linecfg_out);
    gpiod_request_config_free(reqcfg_out);

    // Input lines setup
    struct gpiod_request_config *reqcfg_in = gpiod_request_config_new();
    gpiod_request_config_set_consumer(reqcfg_in, CONSUMER);
    struct gpiod_line_settings *settings_in = gpiod_line_settings_new();
    gpiod_line_settings_set_direction(settings_in, GPIOD_LINE_DIRECTION_INPUT);
    gpiod_line_settings_set_edge_detection(settings_in, GPIOD_LINE_EDGE_BOTH);
    gpiod_line_settings_set_bias(settings_in, GPIOD_LINE_BIAS_DISABLED);
    struct gpiod_line_config *linecfg_in = gpiod_line_config_new();
    unsigned int in_offsets[2] = {DIP_sw.pfo_n, DIP_sw.lim_n};
    gpiod_line_config_add_line_settings(linecfg_in, in_offsets, 2, settings_in);
    in_request = gpiod_chip_request_lines(chip, reqcfg_in, linecfg_in);
    gpiod_line_settings_free(settings_in);
    gpiod_line_config_free(linecfg_in);
    gpiod_request_config_free(reqcfg_in);

    ts.tv_sec = 10;
    ts.tv_nsec = 0;

    lastval_pfo = (uint8_t)gpiod_line_request_get_value(in_request, DIP_sw.pfo_n);
    lastval_lim = (uint8_t)gpiod_line_request_get_value(in_request, DIP_sw.lim_n);

    const char *mains_str = (lastval_pfo == 1) ? "ON" : "OFF";
    const char *lim_str   = (lastval_lim == 1) ? "OFF" : "ON";
    const char *stat_str  = (lastval_pfo == 1) ? "Online" : "On Backup";

    // Handle initial state
    if (lastval_pfo == 0)
        syslog(LOG_INFO, "UPS line power NOK!");
    else
        syslog(LOG_INFO, "UPS line power OK!");

    if (lastval_lim == 0)
        syslog(LOG_INFO, "UPS energy level LOW.");
    else
        syslog(LOG_INFO, "UPS energy level HIGH.");

    if (lastval_pfo == 0 && lastval_lim == 0)
    {
        low_ups_suppressed = 1;
    }

    publish_mqtt_state(mains_str, lim_str, stat_str);

    return 0;
}

int g_gpio_events(void)
{
    if (pthread_create(&g_thread, NULL, &g_callback, NULL) != 0)
    {
        syslog(LOG_ERR, "Thread init failed.");
        return -1;
    }

    if (pthread_create(&g_shdthread, NULL, &g_shdcallback, NULL) != 0)
    {
        syslog(LOG_ERR, "Thread init failed.");
        return -1;
    }

    return 0;
}

// ==========================================
// MAIN ENTRY POINT
// ==========================================
int main(int argc, char **argv)
{
    openlog(CONSUMER, LOG_PID | LOG_NDELAY, LOG_USER);

    // 1. Pass 1: Scan for --config option first
    for (int i = 1; i < argc; i++)
    {
        if (strcmp(argv[i], "--config") == 0 && i + 1 < argc)
        {
            load_config_file(argv[i + 1]);
            break;
        }
    }

    // 2. Pass 2: Process CLI arguments (Overrides JSON options)
    for (int i = 1; i < argc; i++)
    {
        if (strcmp(argv[i], "--config") == 0)
        {
            i++; // skip handled arg
        }
        else if (strcmp(argv[i], "--shutdown-delay") == 0 && i + 1 < argc)
        {
            shutdown_delay = atoi(argv[++i]);
        }
        else if (strcmp(argv[i], "--dip") == 0 && i + 1 < argc)
        {
            if (!set_dip_switch(argv[++i])) {
                fprintf(stderr, "Invalid DIP setting provided.\n");
                exit(EXIT_FAILURE);
            }
        }
        else if (strcmp(argv[i], "--chip") == 0 && i + 1 < argc)
        {
            strncpy(chip_path, argv[++i], sizeof(chip_path) - 1);
        }
        else if (strcmp(argv[i], "--mqtt-broker") == 0 && i + 1 < argc)
        {
            strncpy(mqtt_broker, argv[++i], sizeof(mqtt_broker) - 1);
        }
        else if (strcmp(argv[i], "--mqtt-port") == 0 && i + 1 < argc)
        {
            mqtt_port = atoi(argv[++i]);
        }
        else if (strcmp(argv[i], "--mqtt-user") == 0 && i + 1 < argc)
        {
            strncpy(mqtt_user, argv[++i], sizeof(mqtt_user) - 1);
        }
        else if (strcmp(argv[i], "--mqtt-pass") == 0 && i + 1 < argc)
        {
            strncpy(mqtt_pass, argv[++i], sizeof(mqtt_pass) - 1);
        }
    }

    // Verify minimum required GPIO setup
    if (!dip_configured)
    {
        syslog(LOG_ERR, "DIP switch configuration missing!");
        printf("Error: DIP switch configuration not provided in JSON or CLI options.\n");
        printf("Usage: --dip <10|01|11|111|...>\n");
        exit(EXIT_FAILURE);
    }

    syslog(LOG_INFO, "Used pins (BCM) - pfo: %d, lim: %d, shd: %d", DIP_sw.pfo_n, DIP_sw.lim_n, DIP_sw.shd_n);
    syslog(LOG_INFO, "Shutdown delay %d seconds", shutdown_delay);

    // Initialize MQTT & Hardware
    mqtt_init();

    if (!g_gpioinit())
    {
        if (!g_gpio_events())
        {
            pthread_join(g_thread, NULL);
            pthread_join(g_shdthread, NULL);
            g_gpiorelease();
        }
    }

    closelog();
    return 0;
}
