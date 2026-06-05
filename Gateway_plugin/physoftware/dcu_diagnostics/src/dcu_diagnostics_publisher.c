#define _GNU_SOURCE
#define _XOPEN_SOURCE
// Author: Dibyajyoti Jena
// Diagnostics Publisher Implementation
// Date: 2025-08-13
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <time.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <dirent.h>
#include <jansson.h>
#include "MQTTClient.h"

// Battery monitoring definitions
#define VREF 3.3
#define ADC_RESOLUTION 4095.0
#define RAW_0 2720   // 0% battery
#define RAW_50 3200  // 50% battery
#define RAW_100 3410 // 100% battery
#define BAT_0 11.4
#define BAT_50 12.8
#define BAT_100 14.0
#define NUM_ADC_SAMPLES 16
#define SAMPLE_DELAY_US 20000
#define ADC_PATH "/sys/bus/iio/devices/iio:device0/in_voltage3_raw"

#define LOG_DIR "/var/log/dcu_diagnostics"
#define LOG_PREFIX_SYS "system_"
#define LOG_PREFIX_CRASH "crash_"
#define LOG_PREFIX_NET "network_"
#define LOG_PREFIX_RESET "reset_"
#define LOG_PREFIX_RESET_COUNT "reset_count_"
#define LOG_PREFIX_GW "gateway_"
#define LOG_PREFIX_WSBRD "wsbrd_"
#define PUBLISH_INTERVAL 60000

// Helper: get YYYYMMDD string for today
static void format_current_date(char *buf, size_t buflen)
{
    time_t now = time(NULL);
    struct tm t;
    localtime_r(&now, &t);
    strftime(buf, buflen, "%Y%m%d", &t);
}

static int file_is_regular(const char *dir, const char *name)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/%s", dir, name);
    struct stat st;
    if (stat(path, &st) == 0)
        return S_ISREG(st.st_mode);
    return 0;
}

static void purge_expired_logs(const char *prefix)
{
    DIR *dir = opendir(LOG_DIR);
    if (!dir)
        return;
    struct dirent *entry;
    time_t now = time(NULL);
    while ((entry = readdir(dir)))
    {
        if (!file_is_regular(LOG_DIR, entry->d_name))
            continue;
        if (strncmp(entry->d_name, prefix, strlen(prefix)) != 0)
            continue;
        char datepart[9];
        if (sscanf(entry->d_name + strlen(prefix), "%8[0-9]", datepart) == 1)
        {
            struct tm file_tm = {0};
            if (!strptime(datepart, "%Y%m%d", &file_tm))
                continue;
            time_t file_time = mktime(&file_tm);
            double days = difftime(now, file_time) / (60 * 60 * 24);
            if (days > 7.0)
            {
                char path[512];
                snprintf(path, sizeof(path), "%s/%s", LOG_DIR, entry->d_name);
                remove(path);
            }
        }
    }
    closedir(dir);
}

static void ensure_log_directory_exists()
{
    struct stat st;
    if (stat(LOG_DIR, &st) == -1)
    {
        mkdir(LOG_DIR, 0755);
    }
}

static char *execute_system_command(const char *cmd)
{

    FILE *fp = popen(cmd, "r");
    if (!fp)
    {
        fprintf(stderr, "Warning: popen failed for '%s'\n", cmd);
        return strdup("Error: command failed");
    }
    char *buf = malloc(4096);
    if (!buf)
    {
        pclose(fp);
        fprintf(stderr, "Warning: malloc failed for command '%s'\n", cmd);
        return strdup("Error: memory allocation failed");
    }
    buf[0] = '\0';
    int total_read = 0;
    int max_wait = 5; // seconds
    time_t start = time(NULL);
    while (fgets(buf + total_read, 4096 - total_read, fp) != NULL)
    {
        total_read = strlen(buf);
        if (time(NULL) - start > max_wait)
        {
            fprintf(stderr, "Warning: command '%s' timed out\n", cmd);
            break;
        }
    }
    pclose(fp);
    if (total_read == 0)
    {
        free(buf);
        return strdup("");
    }
    return buf;
}

static void append_log_entry(const char *prefix, const char *data)
{
    ensure_log_directory_exists();
    purge_expired_logs(prefix);
    char date[16], path[256], ts[32];
    format_current_date(date, sizeof(date));
    time_t now = time(NULL);
    struct tm t;
    localtime_r(&now, &t);
    strftime(ts, sizeof(ts), "%Y-%m-%dT%H:%M:%SZ", &t);
    snprintf(path, sizeof(path), "%s/%s%s.log", LOG_DIR, prefix, date);
    FILE *f = fopen(path, "a");
    if (f)
    {
        fprintf(f, "[%s] %s\n", ts, data);
        fclose(f);
    }
}

// Diagnostics Gathering
// Remove get_plc_network_info() and get_plc_gateway_info() functions
// Helper: extract global IPv6 address from tun1
static char *get_tun1_global_ipv6()
{
    FILE *fp = popen("ip -6 addr show dev tun1 | grep 'inet6' | grep -v 'fe80' | awk '{print $2}' | cut -d'/' -f1", "r");
    if (!fp)
        return strdup("");
    char buf[128] = "";
    if (fgets(buf, sizeof(buf), fp))
    {
        buf[strcspn(buf, "\n")] = 0;
    }
    pclose(fp);
    return strdup(buf);
}

// Helper: extract EUI-64 MAC from IPv6 address (last 64 bits, format xxxx:xxxx:xxxx:xxxx)
static char *eui64_mac_from_ipv6(const char *ipv6)
{
    if (!ipv6 || strlen(ipv6) < 19)
        return strdup("");
    // Find last 4 ':'
    int colons = 0;
    for (const char *q = ipv6; *q; ++q)
        if (*q == ':')
            colons++;
    if (colons < 4)
        return strdup("");
    // Find pointer to last 4 hextets
    const char *last = ipv6;
    int found = 0;
    for (const char *q = ipv6; *q; ++q)
    {
        if (*q == ':')
            found++;
        if (found == colons - 3)
        {
            last = q + 1;
            break;
        }
    }
    // Parse 4 hextets (e.g., eae:5fff:fe6d:1fb2)
    unsigned int h1, h2, h3, h4;
    if (sscanf(last, "%x:%x:%x:%x", &h1, &h2, &h3, &h4) != 4)
        return strdup("");
    unsigned char mac_bytes[8];
    mac_bytes[0] = (h1 >> 8) & 0xFF;
    mac_bytes[1] = h1 & 0xFF;
    mac_bytes[2] = (h2 >> 8) & 0xFF;
    mac_bytes[3] = h2 & 0xFF;
    mac_bytes[4] = (h3 >> 8) & 0xFF;
    mac_bytes[5] = h3 & 0xFF;
    mac_bytes[6] = (h4 >> 8) & 0xFF;
    mac_bytes[7] = h4 & 0xFF;
    char mac[32];
    snprintf(mac, sizeof(mac), "%02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X",
             mac_bytes[0], mac_bytes[1], mac_bytes[2], mac_bytes[3],
             mac_bytes[4], mac_bytes[5], mac_bytes[6], mac_bytes[7]);
    return strdup(mac);
}
static char *get_system_metrics()
{
    // Get CPU usage using mpstat
    char *mpstat_output = execute_system_command("mpstat 1 1 | tail -n 1");
    if (!mpstat_output)
        return strdup("Error: Unable to get system metrics");

    double cpu_usage = 0.0;

    // Parse mpstat output for CPU idle percentage
    char *last_token = NULL;
    char *token = strtok(mpstat_output, " ");
    // Get the last token which is the idle percentage
    while (token != NULL)
    {
        last_token = token;
        token = strtok(NULL, " ");
    }
    if (last_token)
    {
        double idle;
        if (sscanf(last_token, "%lf", &idle) == 1)
        {
            cpu_usage = 100.0 - idle; // Convert idle to usage percentage
        }
    }
    free(mpstat_output);

    // Get memory information using free -m command for MB values
    char *free_output = execute_system_command("free -m");
    long mem_total = 0, mem_used = 0, mem_avail = 0;
    if (free_output)
    {
        char *line = strtok(free_output, "\n");
        while (line)
        {
            // Memory line format: "Mem:      total     used     free   shared  buff/cache   available"
            if (strstr(line, "Mem:"))
            {
                char dummy1[32], dummy2[32];
                sscanf(line, "Mem: %ld %ld %s %s %s %ld",
                       &mem_total, &mem_used, dummy1, dummy2, dummy2, &mem_avail);
            }
            line = strtok(NULL, "\n");
        }
        free(free_output);
    }

    // Get disk space info using statvfs
    struct statvfs stat;
    double disk_total_gb = 0.0, disk_used_gb = 0.0;
    if (statvfs("/", &stat) == 0)
    {
        // Calculate total size
        unsigned long long total_bytes = (unsigned long long)stat.f_blocks * stat.f_frsize;
        // Calculate available size (considering root reserved space)
        unsigned long long avail_bytes = (unsigned long long)stat.f_bavail * stat.f_frsize;
        // Calculate used size
        unsigned long long used_bytes = total_bytes - ((unsigned long long)stat.f_bfree * stat.f_frsize);

        // Convert to GB
        disk_total_gb = (double)total_bytes / (1024 * 1024 * 1024);
        disk_used_gb = (double)used_bytes / (1024 * 1024 * 1024);
    }

    // Format disk values with 2 decimal places
    char disk_used_str[32], disk_total_str[32];
    snprintf(disk_used_str, sizeof(disk_used_str), "%.2f", disk_used_gb);
    snprintf(disk_total_str, sizeof(disk_total_str), "%.2f", disk_total_gb);

    char buf[512];
    snprintf(buf, sizeof(buf),
             "CPU: %.2f%%, Mem: %ld/%ld MB (Available: %ld MB), Disk: %sGB/%sGB used",
             cpu_usage, mem_used, mem_total, mem_avail,
             disk_used_str, disk_total_str);
    return strdup(buf);
}

// Battery monitoring functions
static int read_adc_raw()
{
    FILE *fp = fopen(ADC_PATH, "r");
    if (!fp)
    {
        fprintf(stderr, "Warning: Failed to open ADC file: %s\n", ADC_PATH);
        return -1;
    }
    int raw = 0;
    if (fscanf(fp, "%d", &raw) != 1)
    {
        fprintf(stderr, "Warning: Failed to read ADC value from %s\n", ADC_PATH);
        raw = -1;
    }
    fclose(fp);
    return raw;
}

static double get_battery_voltage(int raw)
{
    if (raw <= RAW_0)
        return BAT_0;
    if (raw >= RAW_100)
        return BAT_100;
    double slope = (BAT_100 - BAT_0) / (RAW_100 - RAW_0);
    return BAT_0 + slope * (raw - RAW_0);
}

static int get_battery_percentage(double batt_voltage)
{
    if (batt_voltage <= BAT_0)
        return 0;
    if (batt_voltage >= BAT_100)
        return 100;

    if (batt_voltage <= BAT_50)
    {
        double slope = 50.0 / (BAT_50 - BAT_0);
        return (int)(slope * (batt_voltage - BAT_0) + 0.5);
    }
    else
    {
        double slope = 50.0 / (BAT_100 - BAT_50);
        return (int)(50.0 + slope * (batt_voltage - BAT_50) + 0.5);
    }
}

static int get_average_adc_raw()
{
    int sum = 0, count = 0;
    for (int i = 0; i < NUM_ADC_SAMPLES; i++)
    {
        int raw = read_adc_raw();
        if (raw >= 0)
        {
            sum += raw;
            count++;
        }
        usleep(SAMPLE_DELAY_US);
    }
    return count > 0 ? sum / count : -1;
}

static int get_current_battery_percentage()
{
    int raw_avg = get_average_adc_raw();
    if (raw_avg < 0)
    {
        fprintf(stderr, "Warning: Failed to read battery ADC\n");
        return -1;
    }

    double batt_voltage = get_battery_voltage(raw_avg);
    return get_battery_percentage(batt_voltage);
}

static char *get_reset_reason()
{
    char *reg_out = execute_system_command("busybox devmem 0x020D8028");
    uint32_t value = 0;

    // Get timestamp in IST
    struct timespec tspec;
    clock_gettime(CLOCK_REALTIME, &tspec);
    char ist_ts[32];
    struct tm t;
    time_t ist_time = tspec.tv_sec + (5 * 60 * 60) + (30 * 60); // Add 5 hours 30 minutes for IST
    gmtime_r(&ist_time, &t);
    strftime(ist_ts, sizeof(ist_ts), "%Y-%m-%d %H:%M:%S IST", &t);

    char *reason;
    if (reg_out && sscanf(reg_out, "%x", &value) == 1)
    {
        if (value == 0x00000001)
            reason = "Power-On Reset (POR)";
        else if (value == 0x00000010)
            reason = "Watchdog Reset (WDOG)";
        else
        {
            static char unknown[64];
            snprintf(unknown, sizeof(unknown), "Unknown Reset (value: 0x%08X)", value);
            reason = unknown;
        }
    }
    else
    {
        reason = "Error reading register";
    }

    if (reg_out)
        free(reg_out);

    // Format with timestamp
    char *buf = malloc(256);
    if (buf)
    {
        snprintf(buf, 256, "{\"timestamp\":\"%s\",\"cause\":\"%s\"}", ist_ts, reason);
        return buf;
    }
    return strdup("{\"timestamp\":\"\",\"cause\":\"Memory allocation error\"}");
}

static char *get_network_status()
{
    return execute_system_command("ifconfig");
}

static bool is_recent_reboot(void)
{
    FILE *fp = fopen("/proc/uptime", "r");
    if (!fp)
    {
        fprintf(stderr, "Error: Cannot read system uptime\n");
        return false;
    }

    double uptime, idle;
    bool is_reboot = false;

    if (fscanf(fp, "%lf %lf", &uptime, &idle) == 2)
    {
        // Consider it a reboot if system uptime is less than 5 minutes
        is_reboot = (uptime < 300.0); // 300 seconds = 5 minutes
    }

    fclose(fp);
    return is_reboot;
}

static char *get_reset_event_count()
{
    static bool first_run = true;        // Track first execution since boot
    static int current_count = 0;        // Cache the count for subsequent calls
    static char cached_entry[256] = {0}; // Cache the JSON entry

    // If not first run or not a recent reboot, return the cached count
    if (!first_run || !is_recent_reboot())
    {
        if (cached_entry[0] != '\0')
        {
            return strdup(cached_entry);
        }
    }

    ensure_log_directory_exists();

    char date[16];
    format_current_date(date, sizeof(date));

    char path[256];
    snprintf(path, sizeof(path), "%s/%s%s.log", LOG_DIR, LOG_PREFIX_RESET_COUNT, date);

    // Get current timestamp for this boot in IST
    struct timespec tspec;
    clock_gettime(CLOCK_REALTIME, &tspec);
    char ist_ts[32];
    struct tm t;
    time_t ist_time = tspec.tv_sec + (5 * 60 * 60) + (30 * 60); // Add 5 hours 30 minutes for IST
    gmtime_r(&ist_time, &t);
    strftime(ist_ts, sizeof(ist_ts), "%Y-%m-%d %H:%M:%S IST", &t);

    // Read last count from the file
    int count = 0;
    FILE *fp = fopen(path, "r");
    if (fp)
    {
        char line[256];
        // Read the last line to get the latest count
        while (fgets(line, sizeof(line), fp))
        {
            int tmp_count;
            // Parse the JSON to get the count
            char *count_str = strstr(line, "\"count\":");
            if (count_str && sscanf(count_str + 8, "%d", &tmp_count) == 1)
            {
                count = tmp_count;
            }
        }
        fclose(fp);
    }

    // Increment count only on first run (boot)
    count++;
    current_count = count;

    // Create JSON format string
    snprintf(cached_entry, sizeof(cached_entry),
             "{\"timestamp\":\"%s\",\"count\":%d}",
             ist_ts, current_count); // Log the new count on boot
    append_log_entry(LOG_PREFIX_RESET_COUNT, cached_entry);

    // Mark as no longer first run
    first_run = false;

    return strdup(cached_entry);
}

// Configuration struct for runtime parameters (matches schema config fields)

typedef struct
{
    int mqtt_qos;
    int publish_interval_ms;
    char log_buffer_type[16];
    bool tls_enabled;
    int tls_port;
    char mqtt_host[128];
    int mqtt_port;
    char mqtt_topic_template[128]; // e.g. PHY_WISUN/<network_id>/data/<src_mac>/<dst_mac>/dcu_diagnostic
    char dst_mac[32];              // dst_mac
    char network_id[64];
} DiagnosticsConfig;

// Load MQTT config from .conf file
static int load_mqtt_config_conf(const char *filename, DiagnosticsConfig *config)
{
    FILE *f = fopen(filename, "r");
    if (!f)
    {
        fprintf(stderr, "Error opening config file: %s\n", filename);
        return -1;
    }
    char line[256];
    while (fgets(line, sizeof(line), f))
    {
        char key[64], value[128];
        if (sscanf(line, "%63[^=]=%127s", key, value) == 2)
        {
            if (strcmp(key, "mqtt_host") == 0)
            {
                strncpy(config->mqtt_host, value, sizeof(config->mqtt_host));
            }
            else if (strcmp(key, "mqtt_port") == 0)
            {
                config->mqtt_port = atoi(value);
            }
            else if (strcmp(key, "mqtt_topic") == 0)
            {
                strncpy(config->mqtt_topic_template, value, sizeof(config->mqtt_topic_template));
            }
            else if (strcmp(key, "dst_mac") == 0)
            {
                strncpy(config->dst_mac, value, sizeof(config->dst_mac));
                config->dst_mac[sizeof(config->dst_mac) - 1] = '\0';
            }
            else if (strcmp(key, "network_id") == 0)
            {
                strncpy(config->network_id, value, sizeof(config->network_id));
                config->network_id[sizeof(config->network_id) - 1] = '\0';
            }
            else if (strcmp(key, "publish_interval_ms") == 0)
            {
                config->publish_interval_ms = atoi(value);
            }
        }
    }
    fclose(f);
    return 0;
}
// Load config from JSON file matching schema
static int load_diagnostics_config_json(const char *filename, DiagnosticsConfig *config)
{
    json_t *root;
    json_error_t error;
    root = json_load_file(filename, 0, &error);
    if (!root)
    {
        fprintf(stderr, "Error loading config JSON: %s\n", error.text);
        return -1;
    }

    // Continue loading other config fields as before
    json_t *diagnostic = json_object_get(root, "diagnostic");
    if (!diagnostic)
    {
        json_decref(root);
        return -1;
    }
    json_t *mqtt_status = json_object_get(diagnostic, "mqtt_status");
    if (mqtt_status)
    {
        json_t *publishing = json_object_get(mqtt_status, "publishing");
        if (publishing)
        {
            config->mqtt_qos = json_integer_value(json_object_get(publishing, "qos"));
            config->publish_interval_ms = json_integer_value(json_object_get(publishing, "publish_interval_ms"));
        }
        json_t *logging = json_object_get(mqtt_status, "logging");
        if (logging)
        {
            const char *log_type = json_string_value(json_object_get(logging, "log_buffer_type"));
            strncpy(config->log_buffer_type, log_type ? log_type : "file_rotation", sizeof(config->log_buffer_type));
        }
        json_t *security = json_object_get(mqtt_status, "security");
        if (security)
        {
            config->tls_enabled = json_is_true(json_object_get(security, "tls_enabled"));
            config->tls_port = json_integer_value(json_object_get(security, "tls_port"));
        }
    }
    json_decref(root);
    return 0;
}

// Publishing
static void publish_diagnostics_payload(const DiagnosticsConfig *config,
                                        const char *system_metrics, int battery_percentage, const char *reset_reason, const char *network_status,
                                        const char *reset_event_count)
{
    MQTTClient client;
    MQTTClient_connectOptions conn_opts = MQTTClient_connectOptions_initializer;
    char address[256];
    if (config->mqtt_host[0] == '\0' || config->mqtt_port <= 0)
    {
        fprintf(stderr, "[ERROR] MQTT host or port not set in config.\n");
        snprintf(address, sizeof(address), "tcp://localhost:1883");
    }
    else
    {
        snprintf(address, sizeof(address), "tcp://%s:%d", config->mqtt_host, config->mqtt_port);
    }
    printf("[DEBUG] MQTT broker address: %s\n", address);
    // Print the actual topic used for publishing
    // MQTT topic is built below, so print after construction
    MQTTClient_create(&client, address, "dcu_diag_pub", MQTTCLIENT_PERSISTENCE_NONE, NULL);
    conn_opts.keepAliveInterval = 20;
    conn_opts.cleansession = 1;
    int rc = MQTTClient_connect(client, &conn_opts);
    if (rc != MQTTCLIENT_SUCCESS)
    {
        printf("Failed to connect to MQTT broker, return code %d\n", rc);
        MQTTClient_destroy(&client);
        return;
    }
    char payload[16384];
    // Get epoch timestamp in ms
    struct timespec tspec;
    clock_gettime(CLOCK_REALTIME, &tspec);
    long long epoch_ms = (long long)tspec.tv_sec * 1000LL + tspec.tv_nsec / 1000000LL;
    // Get ISO8601 timestamp
    time_t now = tspec.tv_sec;
    struct tm t;
    char iso_ts[32];
    gmtime_r(&now, &t);
    strftime(iso_ts, sizeof(iso_ts), "%Y-%m-%dT%H:%M:%SZ", &t);

    // Fetch src_mac and src_ipv6 from tun1
    char *src_ipv6 = get_tun1_global_ipv6();
    char *src_mac = eui64_mac_from_ipv6(src_ipv6);
    const char *src_type = "gateway";
    const char *dst_ipv6 = "fe80::2"; // Optionally fetch dynamically
    const char *dst_type = "server";

    // Build MQTT topic at runtime using template from config
    char mqtt_topic[256];
    snprintf(mqtt_topic, sizeof(mqtt_topic), "%s", config->mqtt_topic_template);
    // Replace <src_mac> in topic
    char *p = strstr(mqtt_topic, "<src_mac>");
    if (p)
    {
        char tmp[256];
        size_t prefix = p - mqtt_topic;
        snprintf(tmp, sizeof(tmp), "%.*s%s%s", (int)prefix, mqtt_topic, src_mac && src_mac[0] ? src_mac : "unknown", p + strlen("<src_mac>"));
        strncpy(mqtt_topic, tmp, sizeof(mqtt_topic));
    }
    // Replace <dst_mac> in topic
    p = strstr(mqtt_topic, "<dst_mac>");
    if (p)
    {
        char tmp[256];
        size_t prefix = p - mqtt_topic;
        snprintf(tmp, sizeof(tmp), "%.*s%s%s", (int)prefix, mqtt_topic, config->dst_mac[0] ? config->dst_mac : "unknown", p + strlen("<dst_mac>"));
        strncpy(mqtt_topic, tmp, sizeof(mqtt_topic));
    }

    char esc_system_metrics[1024], esc_reset_reason[256], esc_network_status[2048], esc_reset_event_count[64];
    snprintf(esc_system_metrics, sizeof(esc_system_metrics), "%s", system_metrics ? system_metrics : "");
    snprintf(esc_reset_reason, sizeof(esc_reset_reason), "%s", reset_reason ? reset_reason : "");
    snprintf(esc_network_status, sizeof(esc_network_status), "%s", network_status ? network_status : "");
    snprintf(esc_reset_event_count, sizeof(esc_reset_event_count), "%s", reset_event_count ? reset_event_count : "");

    snprintf(payload, sizeof(payload),
             "{\n"
             "  \"ts\": %lld,\n"
             "  \"values\": {\n"
             "    \"timestamp\": \"%s\",\n"
             "    \"msg_type\": \"dcu_diagnostic\",\n"
             "    \"src\": {\n"
             "      \"ipv6\": \"%s\",\n"
             "      \"mac\": \"%s\",\n"
             "      \"device_type\": \"%s\"\n"
             "    },\n"
             "    \"dst\": {\n"
             "      \"ipv6\": \"%s\",\n"
             "      \"mac\": \"%s\",\n"
             "      \"device_type\": \"%s\"\n"
             "    },\n"
             "    \"diagnostic\": {\n"
             "      \"system_metrics\": \"%s\",\n"
             "      \"battery_monitor\": {\n"
             "        \"charge\": %d\n"
             "      },\n"
             "      \"reset_info\": %s,\n"
             "      \"network_status\": \"%s\",\n"
             "      \"reset_count\": %s,\n"
             "      \"mqtt_status\": {\n"
             "        \"connection\": {\"status\": \"connected\"},\n"
             "        \"publishing\": {\"qos\": %d, \"publish_interval_ms\": %d}\n"
             "      }\n"
             "    }\n"
             "  }\n"
             "}\n",

             epoch_ms,
             iso_ts,
             src_ipv6 && src_ipv6[0] ? src_ipv6 : "unknown",
             src_mac && src_mac[0] ? src_mac : "unknown",
             src_type,
             dst_ipv6, config->dst_mac[0] ? config->dst_mac : "unknown", dst_type,
             esc_system_metrics, battery_percentage, esc_reset_reason, esc_network_status, esc_reset_event_count,
             config->mqtt_qos, config->publish_interval_ms);

    if (src_mac)
        free(src_mac);
    if (src_ipv6)
        free(src_ipv6);

    printf("[DEBUG] MQTT topic: %s\n", mqtt_topic);
    printf("[DEBUG] MQTT payload: %s\n", payload);
    MQTTClient_message pubmsg = MQTTClient_message_initializer;
    pubmsg.payload = payload;
    pubmsg.payloadlen = (int)strlen(payload);
    pubmsg.qos = config->mqtt_qos;
    pubmsg.retained = 0;
    MQTTClient_deliveryToken token;
    rc = MQTTClient_publishMessage(client, mqtt_topic, &pubmsg, &token);
    if (rc != MQTTCLIENT_SUCCESS)
        printf("Failed to publish message, return code %d\n", rc);
    else
        printf("Published diagnostics to MQTT topic %s\n", mqtt_topic);
    MQTTClient_waitForCompletion(client, token, 1000L);
    MQTTClient_disconnect(client, 1000);
    MQTTClient_destroy(&client);
}

// Main Loop exposed as reusable function
void run_diagnostics_publisher(void)
{
    DiagnosticsConfig config = {0};
    // Load MQTT config from .conf file (use correct path)
    int conf_ok = load_mqtt_config_conf("/usr/local/bin/PHY/dcu_diagnostics.conf", &config);
    if (conf_ok != 0)
    {
        fprintf(stderr, "[ERROR] Could not load diagnostics config file. Check path and permissions.\n");
        return;
    }
    // Load other config from JSON if needed (optional, skip if missing)
    load_diagnostics_config_json("/usr/local/bin/PHY/data.schema.json", &config);

    while (1)
    {
        char *system_metrics = get_system_metrics();
        char *reset_reason = get_reset_reason();
        char *network_status = get_network_status();
        char *reset_event_count = get_reset_event_count();

        append_log_entry(LOG_PREFIX_SYS, system_metrics ? system_metrics : "");
        append_log_entry(LOG_PREFIX_CRASH, reset_reason ? reset_reason : "");
        append_log_entry(LOG_PREFIX_NET, network_status ? network_status : "");
        append_log_entry(LOG_PREFIX_RESET, reset_event_count ? reset_event_count : "");
        int battery_percentage = get_current_battery_percentage();
        if (battery_percentage < 0)
        {
            battery_percentage = 0; // Default to 0 if reading fails
            fprintf(stderr, "[WARN] Failed to read battery percentage\n");
        }

        publish_diagnostics_payload(&config,
                                    system_metrics ? system_metrics : "",
                                    battery_percentage,
                                    reset_reason ? reset_reason : "",
                                    network_status ? network_status : "",
                                    reset_event_count ? reset_event_count : "");

        if (system_metrics)
            free(system_metrics);
        if (reset_reason)
            free(reset_reason);
        if (network_status)
            free(network_status);
        if (reset_event_count)
            free(reset_event_count);

        // Simulate cache clear every 2 minutes
        static time_t last_cache_clear = 0;
        time_t now = time(NULL);
        if (now - last_cache_clear >= 120)
        {
            int ret = system("echo 3 > /proc/sys/vm/drop_caches");
            if (ret != 0)
            {
                printf("[WARN] drop_caches command failed (code %d)\n", ret);
            }
            else
            {
                printf("[INFO] Cleared system cache using drop_caches\n");
            }
            last_cache_clear = now;
        }
        // Use config.publish_interval_ms for sleep
        int interval = config.publish_interval_ms > 0 ? config.publish_interval_ms : PUBLISH_INTERVAL;
        usleep(interval * 1000);
    }
}
