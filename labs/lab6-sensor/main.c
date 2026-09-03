/* Lab 6: everything from the day in one application.
 * - devicetree: the die temperature sensor enabled by our overlay
 * - Kconfig: sensor, shell and logging subsystems turned on
 * - threads: a sampler thread polls the sensor every second
 * - synchronisation: a mutex guards the shared min/max statistics
 * - interrupts: the button ISR signals a semaphore to force a reading
 * - shell: "temp" prints the latest value, "temp stats" prints min/max
 */
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/logging/log.h>
#include <zephyr/shell/shell.h>

LOG_MODULE_REGISTER(tempmon, LOG_LEVEL_INF);

#define SW0_NODE DT_ALIAS(sw0)

static const struct device *const temp_dev = DEVICE_DT_GET(DT_ALIAS(die_temp0));
static const struct gpio_dt_spec button = GPIO_DT_SPEC_GET(SW0_NODE, gpios);

static struct gpio_callback button_cb;

K_SEM_DEFINE(sample_now, 0, 1);
K_MUTEX_DEFINE(stats_lock);

struct temp_stats {
	struct sensor_value last;
	struct sensor_value min;
	struct sensor_value max;
	uint32_t samples;
};

static struct temp_stats stats;

static int64_t to_milli(const struct sensor_value *v)
{
	return (int64_t)v->val1 * 1000 + v->val2 / 1000;
}

static void record(const struct sensor_value *v)
{
	k_mutex_lock(&stats_lock, K_FOREVER);

	stats.last = *v;
	if (stats.samples == 0U || to_milli(v) < to_milli(&stats.min)) {
		stats.min = *v;
	}
	if (stats.samples == 0U || to_milli(v) > to_milli(&stats.max)) {
		stats.max = *v;
	}
	stats.samples++;

	k_mutex_unlock(&stats_lock);
}

static int read_temp(struct sensor_value *out)
{
	int rc = sensor_sample_fetch(temp_dev);

	if (rc) {
		return rc;
	}
	return sensor_channel_get(temp_dev, SENSOR_CHAN_DIE_TEMP, out);
}

static void button_pressed(const struct device *dev,
			   struct gpio_callback *cb, uint32_t pins)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(cb);
	ARG_UNUSED(pins);

	k_sem_give(&sample_now);
}

static void sampler_thread(void *a, void *b, void *c)
{
	ARG_UNUSED(a);
	ARG_UNUSED(b);
	ARG_UNUSED(c);

	while (1) {
		struct sensor_value val;

		if (read_temp(&val) == 0) {
			record(&val);
		}

		/* Wake early if the button asks for an immediate sample. */
		if (k_sem_take(&sample_now, K_MSEC(1000)) == 0) {
			if (read_temp(&val) == 0) {
				record(&val);
				LOG_INF("button sample: %d.%02d C",
					val.val1, val.val2 / 10000);
			}
		}
	}
}

K_THREAD_DEFINE(sampler_id, 1536, sampler_thread, NULL, NULL, NULL, 5, 0, 0);

static int cmd_temp(const struct shell *sh, size_t argc, char **argv)
{
	struct temp_stats snapshot;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	k_mutex_lock(&stats_lock, K_FOREVER);
	snapshot = stats;
	k_mutex_unlock(&stats_lock);

	if (snapshot.samples == 0U) {
		shell_warn(sh, "no samples yet");
		return 0;
	}

	shell_print(sh, "die temperature: %d.%02d C",
		    snapshot.last.val1, snapshot.last.val2 / 10000);
	return 0;
}

static int cmd_stats(const struct shell *sh, size_t argc, char **argv)
{
	struct temp_stats snapshot;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	k_mutex_lock(&stats_lock, K_FOREVER);
	snapshot = stats;
	k_mutex_unlock(&stats_lock);

	if (snapshot.samples == 0U) {
		shell_warn(sh, "no samples yet");
		return 0;
	}

	shell_print(sh, "samples: %u", snapshot.samples);
	shell_print(sh, "min: %d.%02d C", snapshot.min.val1,
		    snapshot.min.val2 / 10000);
	shell_print(sh, "max: %d.%02d C", snapshot.max.val1,
		    snapshot.max.val2 / 10000);
	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(temp_cmds,
	SHELL_CMD(stats, NULL, "Show min/max/among samples", cmd_stats),
	SHELL_SUBCMD_SET_END
);

SHELL_CMD_REGISTER(temp, &temp_cmds, "Read the SoC die temperature", cmd_temp);

int main(void)
{
	if (!device_is_ready(temp_dev)) {
		LOG_ERR("die temperature sensor not ready");
		return -1;
	}

	if (!gpio_is_ready_dt(&button)) {
		LOG_ERR("button device not ready");
		return -1;
	}

	gpio_pin_configure_dt(&button, GPIO_INPUT);
	gpio_pin_interrupt_configure_dt(&button, GPIO_INT_EDGE_TO_ACTIVE);
	gpio_init_callback(&button_cb, button_pressed, BIT(button.pin));
	gpio_add_callback(button.port, &button_cb);

	LOG_INF("lab6 running: type 'temp' or 'temp stats' in the shell");
	return 0;
}
