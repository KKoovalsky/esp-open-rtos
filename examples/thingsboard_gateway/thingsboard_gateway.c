#include "espressif/esp_common.h"
#include "esp8266.h"
#include "esp/uart.h"
#include <stdio.h>
#include <string.h>
#include <FreeRTOS.h>
#include <task.h>
#include "queue.h"
#include <paho_mqtt_c/MQTTESP8266.h>
#include <paho_mqtt_c/MQTTClient.h>
#include "private_credentials.h"

#define MQTT_ID "ESP8266-TbGateway"
#define MQTT_PASS NULL
#define MQTT_PORT 1883

#define REGISTER_THERMOMETER_MESSAGE "{\"device\":\"Thermometer\"}"
#define REGISTER_BAROMETER_MESSAGE "{\"device\":\"Barometer\"}"

QueueHandle_t mqttQueue;

volatile int timestamp = 1495879489;

enum dataType_e
{
	ATTRIBUTE,
	TELEMETRY
};

enum deviceType_e
{
	THERMOMETER,
	BAROMETER
};

struct dataPack
{
	char value[16];
	char key[16];
	enum dataType_e dataType;
	enum deviceType_e deviceType;
};

void timeTask(void *pvParameters)
{
	TickType_t lastWakeTime;
	lastWakeTime = xTaskGetTickCount();
	for (;;)
	{
		timestamp++;
		vTaskDelayUntil(&lastWakeTime, pdMS_TO_TICKS(1000));
	}
}

int composeJSON(char *msg, struct dataPack *dPack)
{
	int index = 0;
	if (dPack->deviceType == THERMOMETER)
		index = sprintf(msg + index, "{\"Thermometer\":");
	else
		index = sprintf(msg + index, "{\"Barometer\":");

	if (dPack->dataType == TELEMETRY)
	{
		index += sprintf(msg + index, "[{\"ts\":");
		index += sprintf(msg + index, "%d", timestamp);
		index += sprintf(msg + index, "000,\"values\":");
	}

	index += sprintf(msg + index, "{\"");
	int keyLen = strlen(dPack->key);
	memcpy(msg + index, dPack->key, keyLen);
	index += keyLen;
	index += sprintf(msg + index, "\":\"");
	int valueLen = strlen(dPack->value);
	memcpy(msg + index, dPack->value, valueLen);
	index += valueLen;

	if (dPack->dataType == TELEMETRY)
		index += sprintf(msg + index, "\"}}]}");
	else
		index += sprintf(msg + index, "\"}}");

	return index;
}

void thingsboardGatewayTask(void *pvParameters)
{
	struct mqtt_network network;
	mqtt_client_t client = mqtt_client_default;
	uint8_t mqttBuf[128];
	uint8_t mqttReadBuf[128];
	mqtt_packet_connect_data_t data = mqtt_packet_connect_data_initializer;

	sdk_wifi_station_connect();

	data.willFlag = 0;
	data.MQTTVersion = 3;
	data.clientID.cstring = MQTT_ID;
	data.username.cstring = THINGSBOARD_TOKEN;
	data.password.cstring = MQTT_PASS;
	data.keepAliveInterval = 60;
	data.cleansession = 0;

	mqtt_network_new(&network);

	int ret;
	while (1)
	{
		printf("%s %s\n", data.username.cstring, data.clientID.cstring);
		printf("Establishing MQTT connection...\n\r");
		ret = mqtt_network_connect(&network, MQTT_HOST, MQTT_PORT);
		if (ret != MQTT_SUCCESS)
		{
			printf("Error connecting to MQTT server: %d\n\r", ret);
			vTaskDelay(pdMS_TO_TICKS(2000));
			continue;
		}
		else
			printf("Successfully connected to MQTT server\n\r");

		mqtt_client_new(&client, &network, 5000, mqttBuf, sizeof(mqttBuf), mqttReadBuf, sizeof(mqttReadBuf));
		ret = mqtt_connect(&client, &data);
		if (ret != MQTT_SUCCESS)
		{
			printf("Error sending MQTT CONNECT: %d\n\r", ret);
			mqtt_network_disconnect(&network);
			continue;
		}
		else
			printf("MQTT CONNECT sent successfully\n\r");

		mqtt_message_t message;
		message.payload = REGISTER_THERMOMETER_MESSAGE;
		message.payloadlen = sizeof(REGISTER_THERMOMETER_MESSAGE) - 1;
		message.dup = 0;
		message.qos = MQTT_QOS1;
		message.retained = 0;

		ret = mqtt_publish(&client, "v1/gateway/connect", &message);
		if (ret != MQTT_SUCCESS)
		{
			printf("Error while registering thermometer: %d\n\r", ret);
			mqtt_network_disconnect(&network);
			break;
		} else
			printf("Succesfully registered thermometer\n");

		message.payload = REGISTER_BAROMETER_MESSAGE;
		message.payloadlen = sizeof(REGISTER_BAROMETER_MESSAGE) - 1;

		ret = mqtt_publish(&client, "v1/gateway/connect", &message);
		if (ret != MQTT_SUCCESS)
		{
			printf("Error while registering barometer: %d\n\r", ret);
			mqtt_network_disconnect(&network);
			break;
		} else
			printf("Successfully registered barometer\n");

		for (;;)
		{
			struct dataPack dPack;
			xQueueReceive(mqttQueue, &dPack, portMAX_DELAY);

			char payload[128];
			int index = composeJSON(payload, &dPack);

			message.payload = payload;
			message.payloadlen = index;

			ret = mqtt_publish(&client, (dPack.dataType == ATTRIBUTE ? 
				"v1/gateway/attributes" : "v1/gateway/telemetry"), &message);
			if (ret != MQTT_SUCCESS)
			{
				printf("Error while publishing message: %d\n\r", ret);
				mqtt_network_disconnect(&network);
				break;
			} else
				printf("Succesfully sent message:\n%s with len %d\n", payload, index);
		}
	}
}

void fakeThermometerTask(void *pvParameters)
{
	struct dataPack dPack;
	sprintf(dPack.key, "type");
	sprintf(dPack.value, "infrared");
	dPack.dataType = ATTRIBUTE;
	dPack.deviceType = THERMOMETER;
	xQueueSend(mqttQueue, &dPack, 0);
	vTaskDelay(pdMS_TO_TICKS(5000));

	sprintf(dPack.key, "temperature");
	dPack.dataType = TELEMETRY;
	for (;;)
	{
		double temperature = (double)rand() / (double)RAND_MAX * 20.0 + 10.0;
		sprintf(dPack.value, "%f", temperature);
		xQueueSend(mqttQueue, &dPack, 0);
		vTaskDelay(pdMS_TO_TICKS(10 * 1000));
	}
}

void fakeBarometerTask(void *pvParameters)
{
	struct dataPack dPack;
	sprintf(dPack.key, "type");
	sprintf(dPack.value, "MEMS");
	dPack.dataType = ATTRIBUTE;
	dPack.deviceType = BAROMETER;
	xQueueSend(mqttQueue, &dPack, 0);
	vTaskDelay(pdMS_TO_TICKS(5000));

	sprintf(dPack.key, "pressure");
	dPack.dataType = TELEMETRY;
	for (;;)
	{
		double pressure = (double)rand() / (double)RAND_MAX * 100.0 + 950.0;
		sprintf(dPack.value, "%f", pressure);
		xQueueSend(mqttQueue, &dPack, 0);
		vTaskDelay(pdMS_TO_TICKS(10 * 1000));
	}
}

void user_init(void)
{
	uart_set_baud(0, 74880);
	struct sdk_station_config config = {
		.ssid = WIFI_SSID,
		.password = WIFI_PASS,
	};
	sdk_wifi_set_opmode(STATION_MODE);
	sdk_wifi_station_set_config(&config);

	mqttQueue = xQueueCreate(6, sizeof(struct dataPack));

	xTaskCreate(thingsboardGatewayTask, "TbGateway", 1024, NULL, 3, NULL);
	xTaskCreate(fakeBarometerTask, "FakeBarometer", 512, NULL, 2, NULL);
	xTaskCreate(fakeThermometerTask, "FakeThermometer", 512, NULL, 2, NULL);
	xTaskCreate(timeTask, "Time", 256, NULL, 1, NULL);
}