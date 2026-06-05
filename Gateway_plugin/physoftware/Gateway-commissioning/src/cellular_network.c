// cellular_config.c
// Reads APN from dcu_network.conf, configures and starts cellular connection using QMI and udhcpc
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdbool.h>
#include <time.h>
#include "cellular_network.h"

#define CONF_FILE "/etc/qmi-network.conf"
#define APN_CONF_FILE "dcu_network.conf"
#define MODEM_DEV "/dev/cdc-wdm0"
#define IFACE "wwan0"
#define STATUS_FILE "/var/log/network_status.log"

// Helper function to read APN from config file
bool read_apn(char *apn, size_t apn_size)
{
    FILE *f = fopen(APN_CONF_FILE, "r");
    if (!f)
        return false;
    char line[128];
    while (fgets(line, sizeof(line), f))
    {
        if (strncmp(line, "APN=", 4) == 0)
        {
            strncpy(apn, line + 4, apn_size - 1);
            apn[strcspn(apn, "\r\n")] = 0;
            fclose(f);
            return true;
        }
    }
    fclose(f);
    return false;
}

// Helper function to stop QMI connection and flush interface
void stop_qmi_connection()
{
    char state_file[128];
    snprintf(state_file, sizeof(state_file), "/tmp/qmi-network-state-%s", MODEM_DEV + strlen("/dev/"));
    if (access(state_file, F_OK) == 0)
    {
        printf("Stopping existing QMI session...\n");
        int ret1 = system("qmi-network " MODEM_DEV " stop");
        if (ret1 != 0)
            printf("[WARN] qmi-network stop failed (%d)\n", ret1);
        char kill_cmd[128];
        snprintf(kill_cmd, sizeof(kill_cmd), "pkill -f 'udhcpc -i %s'", IFACE);
        int ret2 = system(kill_cmd);
        if (ret2 != 0)
            printf("[WARN] pkill udhcpc failed (%d)\n", ret2);
        char flush_cmd[128];
        snprintf(flush_cmd, sizeof(flush_cmd), "ip addr flush dev %s", IFACE);
        int ret3 = system(flush_cmd);
        if (ret3 != 0)
            printf("[WARN] ip addr flush failed (%d)\n", ret3);
        sleep(2);
    }
}

// Main logic exposed as reusable function
void run_cellular_network(void)
{
    char apn[64] = "";
    if (!read_apn(apn, sizeof(apn)))
    {
        fprintf(stderr, "Error: Could not read APN from %s\n", APN_CONF_FILE);
        return;
    }
    printf("[INFO] APN: %s\n", apn);

    char conf_apn[64] = "";
    FILE *cf = fopen(CONF_FILE, "r");
    if (cf)
    {
        char buf[128];
        while (fgets(buf, sizeof(buf), cf))
        {
            if (strncmp(buf, "APN=", 4) == 0)
            {
                strncpy(conf_apn, buf + 4, sizeof(conf_apn) - 1);
                conf_apn[strcspn(conf_apn, "\r\n")] = 0;
                break;
            }
        }
        fclose(cf);
    }

    bool apn_changed = strcmp(apn, conf_apn) != 0;

    if (apn_changed)
    {
        FILE *cf = fopen(CONF_FILE, "r");
        FILE *tmpf = fopen(CONF_FILE ".tmp", "w");
        if (cf && tmpf)
        {
            char buf[256];
            bool found = false;
            while (fgets(buf, sizeof(buf), cf))
            {
                if (strncmp(buf, "APN=", 4) == 0)
                {
                    fprintf(tmpf, "APN=%s\n", apn);
                    found = true;
                }
                else
                {
                    fputs(buf, tmpf);
                }
            }
            if (!found)
            {
                fprintf(tmpf, "APN=%s\n", apn);
            }
            fclose(cf);
            fclose(tmpf);
            rename(CONF_FILE ".tmp", CONF_FILE);
        }
        else
        {
            FILE *cfnew = fopen(CONF_FILE, "w");
            if (cfnew)
            {
                fprintf(cfnew, "APN=%s\n", apn);
                fclose(cfnew);
            }
        }
        stop_qmi_connection();
        int ret_down = system("ifconfig " IFACE " down");
        if (ret_down != 0)
            printf("[WARN] ifconfig down failed (%d)\n", ret_down);
        int ret_rawip = system("echo Y > /sys/class/net/wwan0/qmi/raw_ip");
        if (ret_rawip != 0)
            printf("[WARN] raw_ip set failed (%d)\n", ret_rawip);
        int ret_up = system("ifconfig " IFACE " up");
        if (ret_up != 0)
            printf("[WARN] ifconfig up failed (%d)\n", ret_up);
        int ret_qmi = system("qmi-network " MODEM_DEV " start");
        if (ret_qmi != 0)
            printf("[WARN] qmi-network start failed (%d)\n", ret_qmi);
        sleep(2);
        char dhcp_cmd[128];
        snprintf(dhcp_cmd, sizeof(dhcp_cmd), "udhcpc -n -q -t 5 -i %s", IFACE);
        int ret_dhcp = system(dhcp_cmd);
        if (ret_dhcp != 0)
            printf("[WARN] udhcpc failed (%d)\n", ret_dhcp);
        FILE *sf = fopen(STATUS_FILE, "a");
        if (sf)
        {
            char ip[32] = "";
            FILE *ipf = popen("ip -4 addr show wwan0 | grep -oP '(?<=inet\\s)\\d+(\\.\\d+){3}'", "r");
            if (ipf && fgets(ip, sizeof(ip), ipf))
            {
                ip[strcspn(ip, "\r\n")] = 0;
                time_t now = time(NULL);
                char ts[32];
                strftime(ts, sizeof(ts), "%Y-%m-%dT%H:%M:%SZ", gmtime(&now));
                fprintf(sf, "%s Cellular: IP ADDRESS %s is connected\n", ts, ip);
            }
            if (ipf)
                pclose(ipf);
            fclose(sf);
        }
        printf("APN updated and GSM restarted successfully\n");
    }
    else
    {
        // Check if wwan0 has a valid IP address
        char ip[32] = "";
        FILE *ipf = popen("ip -4 addr show wwan0 | grep -oP '(?<=inet\\s)\\d+(\\.\\d+){3}'", "r");
        if (ipf && fgets(ip, sizeof(ip), ipf))
        {
            ip[strcspn(ip, "\r\n")] = 0;
        }
        if (ipf)
            pclose(ipf);

        if (ip[0] == '\0')
        {
            // No IP assigned, need to re-establish network
            printf("APN is the same, but no IP assigned. Re-establishing cellular network...\n");
            stop_qmi_connection();
            int ret_down = system("ifconfig " IFACE " down");
            if (ret_down != 0)
                printf("[WARN] ifconfig down failed (%d)\n", ret_down);
            int ret_rawip = system("echo Y > /sys/class/net/wwan0/qmi/raw_ip");
            if (ret_rawip != 0)
                printf("[WARN] raw_ip set failed (%d)\n", ret_rawip);
            int ret_up = system("ifconfig " IFACE " up");
            if (ret_up != 0)
                printf("[WARN] ifconfig up failed (%d)\n", ret_up);
            int ret_qmi = system("qmi-network " MODEM_DEV " start");
            if (ret_qmi != 0)
                printf("[WARN] qmi-network start failed (%d)\n", ret_qmi);
            sleep(2);
            char dhcp_cmd[128];
            snprintf(dhcp_cmd, sizeof(dhcp_cmd), "udhcpc -n -q -t 5 -i %s", IFACE);
            int ret_dhcp = system(dhcp_cmd);
            if (ret_dhcp != 0)
                printf("[WARN] udhcpc failed (%d)\n", ret_dhcp);
            FILE *sf = fopen(STATUS_FILE, "a");
            if (sf)
            {
                char ip2[32] = "";
                FILE *ipf2 = popen("ip -4 addr show wwan0 | grep -oP '(?<=inet\\s)\\d+(\\.\\d+){3}'", "r");
                if (ipf2 && fgets(ip2, sizeof(ip2), ipf2))
                {
                    ip2[strcspn(ip2, "\r\n")] = 0;
                    time_t now = time(NULL);
                    char ts[32];
                    strftime(ts, sizeof(ts), "%Y-%m-%dT%H:%M:%SZ", gmtime(&now));
                    fprintf(sf, "%s Cellular: IP ADDRESS %s is connected\n", ts, ip2);
                }
                if (ipf2)
                    pclose(ipf2);
                fclose(sf);
            }
            printf("Cellular network re-established and IP assigned.\n");
        }
        else
        {
            printf("APN is the same and IP is assigned (%s). No changes required.\n", ip);
        }
    }
}