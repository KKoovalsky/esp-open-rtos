/*
 * Test code for SNTP on esp-open-rtos.
 *
 * Jesus Alonso (doragasu)
 */
#include <espressif/esp_common.h>
#include <esp/uart.h>

#include <string.h>
#include <stdio.h>

#include <FreeRTOS.h>
#include <task.h>

#include <lwip/err.h>
#include <lwip/sockets.h>
#include <lwip/sys.h>
#include <lwip/netdb.h>
#include <lwip/dns.h>
#include <dhcpserver.h>

//#include <ssid_config.h>

/* Add extras/sntp component to makefile for this include to work */
#include <sntp.h>
#include <time.h>

#define SNTP_SERVERS 	"0.pl.pool.ntp.org", "1.pl.pool.ntp.org", \
						"2.pl.pool.ntp.org", "3.pl.pool.ntp.org"



#define vTaskDelayMs(ms)	vTaskDelay((ms)/portTICK_PERIOD_MS)
#define UNUSED_ARG(x)	(void)x

#define WIFI_SSID "Juno_"
#define WIFI_PASS "huehuehue"

#define STATION_SSID "L50 Sporty"
#define STATION_PASS "huehuehue"

static void setStationAPMode()
{
	sdk_wifi_set_opmode(STATIONAP_MODE);
	struct ip_info ap_ip;
	IP4_ADDR(&ap_ip.ip, 192, 168, 1, 1);
	IP4_ADDR(&ap_ip.gw, 0, 0, 0, 0);
	IP4_ADDR(&ap_ip.netmask, 255, 255, 255, 0);
	sdk_wifi_set_ip_info(1, &ap_ip);

	struct sdk_softap_config ap_config = {
		.ssid = WIFI_SSID,
		.ssid_len = strlen(WIFI_SSID),
		.ssid_hidden = 0,
		.channel = 3,
		.authmode = AUTH_WPA_WPA2_PSK,
		.password = WIFI_PASS,
		.max_connection = 3,
		.beacon_interval = 100,
	};

	char macAddr[6], macAddrStr[20];
	sdk_wifi_get_macaddr(STATION_IF, (uint8_t *)macAddr);
	snprintf(macAddrStr, sizeof(macAddrStr), "%02x%02x%02x%02x%02x%02x", MAC2STR(macAddr));
	memcpy(ap_config.ssid + ap_config.ssid_len, macAddrStr, 12);
	ap_config.ssid_len += 12;
	ap_config.ssid[ap_config.ssid_len] = '\0';

	printf("Starting AP with SSID: %s\n", ap_config.ssid);
	sdk_wifi_softap_set_config(&ap_config);

/*
	ip_addr_t first_client_ip;
	IP4_ADDR(&first_client_ip, 192, 168, 1, 2);
	dhcpserver_start(&first_client_ip, 3); */
}

void sntp_tsk(void *pvParameters)
{
	char *servers[] = {SNTP_SERVERS};
	UNUSED_ARG(pvParameters);

	sdk_wifi_station_connect();


	/* Wait until we have joined AP and are assigned an IP */
	while (sdk_wifi_station_get_connect_status() != STATION_GOT_IP) {
		vTaskDelayMs(100);
		printf("Not having IP\n");
	}

	/* Start SNTP */
	printf("Starting SNTP... ");
	/* SNTP will request an update each 5 minutes */
	sntp_set_update_delay(5*60000);
	/* Set GMT+1 zone, daylight savings off */
	const struct timezone tz = {1*60, 0};
	/* SNTP initialization */
	sntp_initialize(&tz);
	/* Servers must be configured right after initialization */
	sntp_set_servers(servers, sizeof(servers) / sizeof(char*));
	printf("DONE!\n");

	/* Print date and time each 5 seconds */
	while(1) {
		vTaskDelayMs(5000);
		time_t ts = time(NULL);
		printf("TIME: %s", ctime(&ts));
	}
}

void user_init(void)
{
    uart_set_baud(0, 74880);
    printf("SDK version:%s\n", sdk_system_get_sdk_version());

    struct sdk_station_config config = {
        .ssid = STATION_SSID,
        .password = STATION_PASS,
    };

    setStationAPMode();
    
    /* required to call wifi_set_opmode before station_set_config */
   // sdk_wifi_set_opmode(STATION_MODE);
    sdk_wifi_station_set_config(&config);

    xTaskCreate(sntp_tsk, "SNTP", 1024, NULL, 1, NULL);
}

