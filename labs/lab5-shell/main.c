/* Lab 5: register a custom shell command that reads the SoC die
 * temperature on demand. The console is already wired to the shell
 * (zephyr,shell-uart = &uart0).
 */
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/shell/shell.h>

static const struct device *const temp_dev = DEVICE_DT_GET(DT_ALIAS(die_temp0));

static int cmd_temp(const struct shell *sh, size_t argc, char **argv)
{
	struct sensor_value val;
	int rc;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	if (!device_is_ready(temp_dev)) {
		shell_error(sh, "sensor not ready");
		return -ENODEV;
	}

	rc = sensor_sample_fetch(temp_dev);
	if (rc) {
		shell_error(sh, "fetch failed (%d)", rc);
		return rc;
	}

	rc = sensor_channel_get(temp_dev, SENSOR_CHAN_DIE_TEMP, &val);
	if (rc) {
		shell_error(sh, "get failed (%d)", rc);
		return rc;
	}

	shell_print(sh, "die temperature: %d.%02d C", val.val1, val.val2 / 10000);
	return 0;
}

SHELL_CMD_REGISTER(temp, NULL, "Read the SoC die temperature", cmd_temp);

int main(void)
{
	return 0;
}
