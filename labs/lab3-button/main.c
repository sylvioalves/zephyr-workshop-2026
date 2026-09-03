/* Lab 3: read the boot button through a GPIO interrupt and report presses
 * with the logging subsystem. The board exposes no GPIO led0, so the console
 * is our output device.
 */
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(btn, LOG_LEVEL_INF);

#define SW0_NODE DT_ALIAS(sw0)

static const struct gpio_dt_spec button =
	GPIO_DT_SPEC_GET(SW0_NODE, gpios);

static struct gpio_callback button_cb;
static volatile uint32_t press_count;

static void button_pressed(const struct device *dev,
			   struct gpio_callback *cb, uint32_t pins)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(cb);
	ARG_UNUSED(pins);

	press_count++;
	LOG_INF("button pressed (#%u) at %u ms", press_count, k_uptime_get_32());
}

int main(void)
{
	if (!gpio_is_ready_dt(&button)) {
		LOG_ERR("button device not ready");
		return -1;
	}

	gpio_pin_configure_dt(&button, GPIO_INPUT);
	gpio_pin_interrupt_configure_dt(&button, GPIO_INT_EDGE_TO_ACTIVE);

	gpio_init_callback(&button_cb, button_pressed, BIT(button.pin));
	gpio_add_callback(button.port, &button_cb);

	LOG_INF("press the boot button");
	return 0;
}
