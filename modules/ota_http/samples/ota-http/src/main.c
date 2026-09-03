#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/dfu/mcuboot.h>

#include <ota_http.h>

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

/* Bump this in the image you serve, so you can tell the two apart on the
 * console after a swap.
 */
#define APP_VERSION "1"

int main(void)
{
	LOG_INF("ota-http sample, version " APP_VERSION);

	if (ota_http_pending_confirm()) {
		LOG_WRN("this image is running on trial");
		LOG_WRN("run 'ota confirm' to keep it, or reboot to go back");
	} else {
		LOG_INF("running a confirmed image");
	}

	LOG_INF("connect to a network, then:");
	LOG_INF("  ota download http://<host>/<image>.signed.bin");
	LOG_INF("  ota apply");
	LOG_INF("  ota reboot");

	return 0;
}
