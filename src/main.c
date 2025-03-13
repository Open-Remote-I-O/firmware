#include <stdio.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/led_strip.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/wifi_mgmt.h>
#include <zephyr/net/dhcpv4_server.h>
#include <esp_wifi.h>

LOG_MODULE_REGISTER(main, LOG_LEVEL_DBG);

#define STRIP_NODE		DT_ALIAS(led_strip)
#define RGB(_r, _g, _b) { .r = (_r), .g = (_g), .b = (_b) }

#define USR_LED_RED 0
#define USR_LED_GREEN 1
#define USR_LED_BLUE 2

#if DT_NODE_HAS_PROP(DT_ALIAS(led_strip), chain_length)
#define STRIP_NUM_PIXELS	DT_PROP(DT_ALIAS(led_strip), chain_length)
#else
#error Unable to determine length of LED strip
#endif

#define MACSTR "%02X:%02X:%02X:%02X:%02X:%02X"

//Subscribe to the event you want to receive in the callback
#define NET_EVENT_WIFI_MASK (NET_EVENT_WIFI_CONNECT_RESULT | NET_EVENT_WIFI_DISCONNECT_RESULT)

//Wifi configurations
#define WIFI_SSID 
#define WIFI_PSK  

static struct led_rgb colors[] = {
	RGB(0x08, 0x00, 0x00), /* red */
	RGB(0x00, 0x08, 0x00), /* green */
	RGB(0x00, 0x00, 0x08), /* blue */
};

static const struct device *const strip = DEVICE_DT_GET(STRIP_NODE);

static struct net_if *sta_iface;

static struct wifi_connect_req_params sta_config;

static struct net_mgmt_event_callback cb;

static bool wifi_connected = false;

static void wifi_event_handler(struct net_mgmt_event_callback *cb, uint32_t mgmt_event, struct net_if *iface)
{
	//Handle the previously subscribed events
	switch (mgmt_event) {
	case NET_EVENT_WIFI_CONNECT_RESULT: {
		wifi_connected = true;
		LOG_INF("Connected to %s", WIFI_SSID);
		led_strip_update_rgb(strip, &colors[USR_LED_GREEN], STRIP_NUM_PIXELS);
		break;
	}
	case NET_EVENT_WIFI_DISCONNECT_RESULT: {
		wifi_connected = false;
		LOG_INF("Disconnected from %s", WIFI_SSID);
		led_strip_update_rgb(strip, &colors[USR_LED_RED], STRIP_NUM_PIXELS);
		break;
	}
	default:
		break;
	}
}


static int connect_to_wifi(void)
{
	//Make sure the interface was previously created by the user
	if (!sta_iface) {
		LOG_INF("STA: interface no initialized");
		return -EIO;
	}

	//Configure the wifi connection
	sta_config.ssid = (const uint8_t *)WIFI_SSID;
	sta_config.ssid_length = strlen(WIFI_SSID);
	sta_config.psk = (const uint8_t *)WIFI_PSK;
	sta_config.psk_length = strlen(WIFI_PSK);
	sta_config.security = WIFI_SECURITY_TYPE_PSK;
	sta_config.channel = WIFI_CHANNEL_ANY;
	sta_config.band = WIFI_FREQ_BAND_2_4_GHZ;

	//Request the wifi connection
	LOG_INF("Connecting to SSID: %s", sta_config.ssid);
	int ret = net_mgmt(NET_REQUEST_WIFI_CONNECT, sta_iface, &sta_config, sizeof(struct wifi_connect_req_params));
	
	//Check for errors
	if (ret) {
		LOG_ERR("Unable to Connect to (%s)", WIFI_SSID);
	} else {

		//Exit the espressif wifi PowerSave mode
		if (esp_wifi_set_ps(WIFI_PS_NONE)){
			LOG_ERR("ESP32_API: Unable to disable power saving");
		} else {
			LOG_INF("ESP32_API: Power saving disabled");
		} 
	}
	return ret;
}

static void get_wifi_rssi(void)
{
    struct wifi_iface_status status = {0};

    if (net_mgmt(NET_REQUEST_WIFI_IFACE_STATUS, sta_iface, &status, sizeof(status)) == 0) {
		LOG_INF("RSSI: %d dBm", status.rssi);
    } else {
        LOG_ERR("Failed to get Wi-Fi status");
    }
}

int main(void)
{
	printk("_______       _____\n"); 
	printk("__  __ \\_________(_)_____\n");
	printk("_  / / /_  ___/_  /_  __ \\\n");
	printk("/ /_/ /_  /   _  / / /_/ /\n");
	printk("\\____/ /_/    /_/  \\____/\n");
	printk("Orio is booting up...\n");

	//Setup the led and set it blue
	if (device_is_ready(strip)) {
		LOG_INF("LED ready");
	} else {
		LOG_ERR("LED not ready");
		return 0;
	}

	for(uint8_t i = 0; i < 2; i++){
		led_strip_update_rgb(strip, &colors[USR_LED_BLUE], STRIP_NUM_PIXELS);
	}

	//Setup wifi callbacks
	net_mgmt_init_event_callback(&cb, wifi_event_handler, NET_EVENT_WIFI_MASK);
	net_mgmt_add_event_callback(&cb);

	//Get wifi interface and connect
	sta_iface = net_if_get_wifi_sta();
	connect_to_wifi();
	
	while (1) {
		k_sleep(K_MSEC(2000));
		if (wifi_connected) {
			get_wifi_rssi();
		}
	}
	
	LOG_INF("Exit main");
	return 0;
}