/* Lab 4: two threads plus one semaphore.
 * - A heartbeat thread logs once per second and sleeps (cooperative yield).
 * - A worker thread blocks on a semaphore that the button ISR gives.
 * The board exposes no GPIO led0, so both threads report on the console.
 */
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(threads, LOG_LEVEL_INF);

#define SW0_NODE DT_ALIAS(sw0)

static const struct gpio_dt_spec button =
	GPIO_DT_SPEC_GET(SW0_NODE, gpios);

static struct gpio_callback button_cb;

K_SEM_DEFINE(button_sem, 0, 1);

static void button_pressed(const struct device *dev,
			   struct gpio_callback *cb, uint32_t pins)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(cb);
	ARG_UNUSED(pins);

	/* Never do slow work in an ISR: signal the worker and return. */
	k_sem_give(&button_sem);
}

static void heartbeat_thread(void *a, void *b, void *c)
{
	ARG_UNUSED(a);
	ARG_UNUSED(b);
	ARG_UNUSED(c);

	while (1) {
		LOG_INF("heartbeat: up for %u ms", k_uptime_get_32());
		k_msleep(1000);
	}
}

static void worker_thread(void *a, void *b, void *c)
{
	ARG_UNUSED(a);
	ARG_UNUSED(b);
	ARG_UNUSED(c);

	while (1) {
		k_sem_take(&button_sem, K_FOREVER);
		LOG_INF("worker woke up from the button ISR");
	}
}

K_THREAD_DEFINE(heartbeat_id, 1024, heartbeat_thread, NULL, NULL, NULL, 5, 0, 0);
K_THREAD_DEFINE(worker_id, 1024, worker_thread, NULL, NULL, NULL, 5, 0, 0);

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

	return 0;
}
