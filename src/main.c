#include <stdio.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/led_strip.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/wifi_mgmt.h>
#include <esp_wifi.h>
#include "secret.h"
#include <zephyr/net/net_if.h>
#include <zephyr/net/dhcpv4.h>

#include <zephyr/net/net_core.h>
#include <zephyr/net/net_mgmt.h>
#include <zephyr/net/dns_resolve.h>

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
#define NET_EVENT_WIFI_MASK (NET_EVENT_WIFI_CONNECT_RESULT | NET_EVENT_WIFI_DISCONNECT_RESULT | NET_EVENT_IPV4_ADDR_ADD)

static struct led_rgb colors[] = {
	RGB(0x08, 0x00, 0x00), /* red */
	RGB(0x00, 0x08, 0x00), /* green */
	RGB(0x00, 0x00, 0x08), /* blue */
};

static const struct device *const strip = DEVICE_DT_GET(STRIP_NODE);

static struct net_if *sta_iface;

static struct wifi_connect_req_params sta_config;

static struct net_mgmt_event_callback wifi_cb;
static struct net_mgmt_event_callback ipv4_cb;

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

static void ipv4_event_handler(struct net_mgmt_event_callback *cb, uint32_t mgmt_event, struct net_if *iface) {
    if (mgmt_event == NET_EVENT_IPV4_ADDR_ADD) {
        
		for (int i = 0; i < NET_IF_MAX_IPV4_ADDR; i++) {
			char buf[NET_IPV4_ADDR_LEN];

			if (iface->config.ip.ipv4->unicast[i].ipv4.addr_type !=
								NET_ADDR_DHCP) {
				continue;
			}

			LOG_INF("   Address[%d]: %s", net_if_get_by_iface(iface), net_addr_ntop(AF_INET, &iface->config.ip.ipv4->unicast[i].ipv4.address.in_addr, buf, sizeof(buf)));
			LOG_INF("    Subnet[%d]: %s", net_if_get_by_iface(iface), net_addr_ntop(AF_INET, &iface->config.ip.ipv4->unicast[i].netmask, buf, sizeof(buf)));
			LOG_INF("    Router[%d]: %s", net_if_get_by_iface(iface), net_addr_ntop(AF_INET, &iface->config.ip.ipv4->gw, buf, sizeof(buf)));
			LOG_INF("Lease time[%d]: %u seconds", net_if_get_by_iface(iface), iface->config.dhcpv4.lease_time);
		}
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

void dns_result_cb(enum dns_resolve_status status, struct dns_addrinfo *info, void *user_data)
{
	char hr_addr[NET_IPV6_ADDR_LEN];
	char *hr_family;
	void *addr;

	switch (status) {
	case DNS_EAI_CANCELED:
		LOG_INF("DNS query was canceled");
		return;
	case DNS_EAI_FAIL:
		LOG_INF("DNS resolve failed");
		return;
	case DNS_EAI_NODATA:
		LOG_INF("Cannot resolve address");
		return;
	case DNS_EAI_ALLDONE:
		LOG_INF("DNS resolving finished");
		return;
	case DNS_EAI_INPROGRESS:
		break;
	default:
		LOG_INF("DNS resolving error (%d)", status);
		return;
	}

	if (!info) {
		return;
	}

	if (info->ai_family == AF_INET) {
		hr_family = "IPv4";
		addr = &net_sin(&info->ai_addr)->sin_addr;
	} else if (info->ai_family == AF_INET6) {
		hr_family = "IPv6";
		addr = &net_sin6(&info->ai_addr)->sin6_addr;
	} else {
		LOG_ERR("Invalid IP address family %d", info->ai_family);
		return;
	}

	LOG_INF("%s %s address: %s", user_data ? (char *)user_data : "<null>",
		hr_family,
		net_addr_ntop(info->ai_family, addr,
					 hr_addr, sizeof(hr_addr)));
}

static void do_ipv4_lookup(void)
{
	static const char *query = "pool.ntp.org";
	static uint16_t dns_id;
	int ret;

	ret = dns_get_addr_info(query, DNS_QUERY_TYPE_A, &dns_id, dns_result_cb, (void *)query, 2000);
	if (ret < 0) {
		LOG_ERR("Cannot resolve IPv4 address (%d)", ret);
		return;
	}

	LOG_DBG("DNS id %u", dns_id);
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

	/* Register IPv4 callback */
    net_mgmt_init_event_callback(&wifi_cb, wifi_event_handler, NET_EVENT_WIFI_CONNECT_RESULT | NET_EVENT_WIFI_DISCONNECT_RESULT);
    net_mgmt_add_event_callback(&wifi_cb);

    /* Register IPv4 callback */
    net_mgmt_init_event_callback(&ipv4_cb, ipv4_event_handler, NET_EVENT_IPV4_ADDR_ADD);
    net_mgmt_add_event_callback(&ipv4_cb);

	//Get wifi interface and connect
	sta_iface = net_if_get_wifi_sta();
	connect_to_wifi();

	//Wait for connection
	while(!wifi_connected){
		k_sleep(K_MSEC(100));
	}

	LOG_INF("Wait 10s");
	k_sleep(K_MSEC(10000));

	do_ipv4_lookup();
	
	struct net_if *iface = net_if_get_default();

    if (!iface) {
        printk("No network interface found\n");
    }


	while (1) {
		k_sleep(K_MSEC(2000));
		// if (wifi_connected) {
		// 	get_wifi_rssi();
		// }
	}
	
	LOG_INF("Exit main");
	return 0;
}