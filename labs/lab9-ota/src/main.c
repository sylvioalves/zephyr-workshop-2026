/* Lab 9: join the network from Kconfig at boot, then wait for an OTA. The
 * update itself is the ota_http module, added to the build as a west module.
 */
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_mgmt.h>
#include <zephyr/net/net_event.h>
#include <zephyr/net/wifi_mgmt.h>

#include <ota_http.h>

LOG_MODULE_REGISTER(lab9, LOG_LEVEL_INF);

/* Bump this before publishing an update, so the console tells the two
 * versions apart after the swap.
 */
#define APP_VERSION "1"

static K_SEM_DEFINE(got_ip, 0, 1);
static struct net_mgmt_event_callback ip_cb;

static void on_ip(struct net_mgmt_event_callback *cb, uint64_t event,
		  struct net_if *iface)
{
	ARG_UNUSED(cb);
	ARG_UNUSED(iface);

	if (event == NET_EVENT_IPV4_ADDR_ADD) {
		k_sem_give(&got_ip);
	}
}

static int join_network(void)
{
	struct net_if *iface = net_if_get_first_wifi();
	struct wifi_connect_req_params p = {0};
	int ret;

	if (iface == NULL) {
		LOG_ERR("no wifi interface");
		return -ENODEV;
	}

	p.ssid = (const uint8_t *)CONFIG_LAB9_WIFI_SSID;
	p.ssid_length = strlen(CONFIG_LAB9_WIFI_SSID);
	p.channel = WIFI_CHANNEL_ANY;
	p.band = WIFI_FREQ_BAND_UNKNOWN;
	p.mfp = WIFI_MFP_OPTIONAL;

	if (strlen(CONFIG_LAB9_WIFI_PSK) > 0) {
		p.psk = (const uint8_t *)CONFIG_LAB9_WIFI_PSK;
		p.psk_length = strlen(CONFIG_LAB9_WIFI_PSK);
		p.security = WIFI_SECURITY_TYPE_PSK;
	} else {
		p.security = WIFI_SECURITY_TYPE_NONE;
	}

	for (int attempt = 1; attempt <= CONFIG_LAB9_WIFI_RETRIES; attempt++) {
		LOG_INF("joining %s (attempt %d/%d)", CONFIG_LAB9_WIFI_SSID,
			attempt, CONFIG_LAB9_WIFI_RETRIES);

		ret = net_mgmt(NET_REQUEST_WIFI_CONNECT, iface, &p, sizeof(p));
		if (ret == 0 && k_sem_take(&got_ip, K_SECONDS(20)) == 0) {
			return 0;
		}

		LOG_WRN("no address yet, retrying");
		k_sleep(K_SECONDS(3));
	}

	return -ETIMEDOUT;
}

int main(void)
{
	char buf[NET_IPV4_ADDR_LEN];
	struct net_if *iface;

	LOG_INF("lab9 ota, version " APP_VERSION);

	if (ota_http_pending_confirm()) {
		LOG_WRN("this image is on trial, run 'ota confirm' to keep it");
	}

	net_mgmt_init_event_callback(&ip_cb, on_ip, NET_EVENT_IPV4_ADDR_ADD);
	net_mgmt_add_event_callback(&ip_cb);

	if (join_network() != 0) {
		LOG_ERR("could not join %s", CONFIG_LAB9_WIFI_SSID);
		LOG_ERR("check the ssid and password in prj.conf");
		return 0;
	}

	iface = net_if_get_first_wifi();
	net_addr_ntop(AF_INET, &iface->config.ip.ipv4->unicast[0].ipv4.address.in_addr,
		      buf, sizeof(buf));
	LOG_INF("connected, address %s", buf);
	LOG_INF("now run: ota download http://<servidor>:8000/lab9.signed.bin");

	return 0;
}
