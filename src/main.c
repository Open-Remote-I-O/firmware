#include <stdio.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/led_strip.h>
#include <zephyr/logging/log.h>

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
#define DELAY_TIME K_MSEC(500)

static struct led_rgb colors[] = {
	RGB(0x08, 0x00, 0x00), /* red */
	RGB(0x00, 0x08, 0x00), /* green */
	RGB(0x00, 0x00, 0x08), /* blue */
};


static const struct device *const strip = DEVICE_DT_GET(STRIP_NODE);

int main(void)
{
	// printk("Hello World! %s\n", CONFIG_ARCH);
	
	if (device_is_ready(strip)) {
		LOG_INF("LED ready");
	} else {
		LOG_ERR("LED not ready");
		return 0;
	}

	LOG_INF("Displaying pattern on strip");

	for(uint8_t i = 0; i < 2; i++){
		led_strip_update_rgb(strip, &colors[USR_LED_BLUE], STRIP_NUM_PIXELS);
	}

	LOG_INF("Entering while");
	while (1) {
		k_sleep(K_MSEC(500));
	}
	
	LOG_INF("Exit main");
	return 0;
}
