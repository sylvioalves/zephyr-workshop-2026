/* Lab 8: broadcast the SoC die temperature in the BLE advertising payload.
 * No connection and no pairing: any scanner app reads the manufacturer
 * data straight out of the advertisement.
 */
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/logging/log.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/sys/byteorder.h>

LOG_MODULE_REGISTER(ble_temp, LOG_LEVEL_INF);

#define COMPANY_ID 0xffff
#define SAMPLE_PERIOD K_SECONDS(1)

static const struct device *const temp_dev = DEVICE_DT_GET(DT_ALIAS(die_temp0));

static struct {
	uint16_t company_id;
	int16_t temp_centi;
} __packed mfg_data = {
	.company_id = sys_cpu_to_le16(COMPANY_ID),
};

static char dev_name[sizeof(CONFIG_BT_DEVICE_NAME) + 5];

static struct bt_data ad[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, BT_LE_AD_NO_BREDR),
	BT_DATA(BT_DATA_MANUFACTURER_DATA, &mfg_data, sizeof(mfg_data)),
	BT_DATA(BT_DATA_NAME_COMPLETE, dev_name, 0),
};

static int read_temp_centi(int16_t *out)
{
	struct sensor_value val;
	int rc = sensor_sample_fetch(temp_dev);

	if (rc) {
		return rc;
	}

	rc = sensor_channel_get(temp_dev, SENSOR_CHAN_DIE_TEMP, &val);
	if (rc) {
		return rc;
	}

	*out = (int16_t)(val.val1 * 100 + val.val2 / 10000);
	return 0;
}

/* Append the last two bytes of our own address, so two boards that chose
 * the same name are still told apart in the room.
 */
static void name_with_address(void)
{
	bt_addr_le_t addr;
	size_t count = 1;

	bt_id_get(&addr, &count);

	if (count == 0U) {
		snprintk(dev_name, sizeof(dev_name), CONFIG_BT_DEVICE_NAME);
	} else {
		snprintk(dev_name, sizeof(dev_name), "%s-%02x%02x",
			 CONFIG_BT_DEVICE_NAME, addr.a.val[1], addr.a.val[0]);
	}

	bt_set_name(dev_name);
	ad[2].data_len = strlen(dev_name);
}

int main(void)
{
	int16_t centi;
	int rc;

	if (!device_is_ready(temp_dev)) {
		LOG_ERR("die temperature sensor not ready");
		return -1;
	}

	rc = bt_enable(NULL);
	if (rc) {
		LOG_ERR("bluetooth init failed (%d)", rc);
		return rc;
	}

	name_with_address();

	rc = read_temp_centi(&centi);
	if (rc) {
		LOG_ERR("first sensor read failed (%d)", rc);
		return rc;
	}

	mfg_data.temp_centi = sys_cpu_to_le16(centi);

	rc = bt_le_adv_start(BT_LE_ADV_NCONN_IDENTITY, ad, ARRAY_SIZE(ad),
			     NULL, 0);
	if (rc) {
		LOG_ERR("advertising failed to start (%d)", rc);
		return rc;
	}

	LOG_INF("advertising as %s", dev_name);

	while (1) {
		k_sleep(SAMPLE_PERIOD);

		if (read_temp_centi(&centi)) {
			continue;
		}

		mfg_data.temp_centi = sys_cpu_to_le16(centi);

		rc = bt_le_adv_update_data(ad, ARRAY_SIZE(ad), NULL, 0);
		if (rc) {
			LOG_WRN("advertising update failed (%d)", rc);
			continue;
		}

		LOG_INF("broadcasting %d.%02d C", centi / 100, centi % 100);
	}

	return 0;
}
