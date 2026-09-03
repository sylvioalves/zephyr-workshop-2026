#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/socket.h>
#include <zephyr/net/http/client.h>
#include <zephyr/net/http/parser_url.h>
#include <zephyr/dfu/flash_img.h>
#include <zephyr/dfu/mcuboot.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>

#if defined(CONFIG_OTA_HTTP_TLS)
#include <zephyr/net/tls_credentials.h>
#endif

#include <ota_http.h>

LOG_MODULE_REGISTER(ota_http, CONFIG_OTA_HTTP_LOG_LEVEL);

#define HOST_MAX 128
#define PATH_MAX 256
#define PORT_MAX 8

struct dl_ctx {
	struct flash_img_context flash;
	struct ota_http_progress progress;
	ota_http_progress_cb_t cb;
	void *user_data;
	int err;
	bool flash_ready;
};

static struct dl_ctx dl;
static uint8_t recv_buf[CONFIG_OTA_HTTP_RECV_BUF_SIZE];

static void cancel_revert_timer(void);

struct parsed_url {
	char host[HOST_MAX];
	char path[PATH_MAX];
	char port[PORT_MAX];
	bool tls;
};

static int parse_url(const char *url, struct parsed_url *out)
{
	struct http_parser_url u;
	size_t len = strlen(url);
	uint16_t off, flen;

	http_parser_url_init(&u);
	if (http_parser_parse_url(url, len, 0, &u) != 0) {
		return -EINVAL;
	}

	if (!(u.field_set & (1 << UF_HOST))) {
		return -EINVAL;
	}

	flen = u.field_data[UF_HOST].len;
	off = u.field_data[UF_HOST].off;
	if (flen >= sizeof(out->host)) {
		return -EINVAL;
	}
	memcpy(out->host, url + off, flen);
	out->host[flen] = '\0';

	out->tls = false;
	if (u.field_set & (1 << UF_SCHEMA)) {
		flen = u.field_data[UF_SCHEMA].len;
		off = u.field_data[UF_SCHEMA].off;
		if (flen == 5 && strncmp(url + off, "https", 5) == 0) {
			out->tls = true;
		} else if (!(flen == 4 && strncmp(url + off, "http", 4) == 0)) {
			return -ENOTSUP;
		}
	}

	if (u.field_set & (1 << UF_PORT)) {
		snprintk(out->port, sizeof(out->port), "%u", u.port);
	} else {
		strcpy(out->port, out->tls ? "443" : "80");
	}

	if (u.field_set & (1 << UF_PATH)) {
		flen = u.field_data[UF_PATH].len;
		off = u.field_data[UF_PATH].off;
		if (flen >= sizeof(out->path)) {
			return -EINVAL;
		}
		memcpy(out->path, url + off, flen);
		out->path[flen] = '\0';
	} else {
		strcpy(out->path, "/");
	}

	if (u.field_set & (1 << UF_QUERY)) {
		size_t used = strlen(out->path);
		flen = u.field_data[UF_QUERY].len;
		off = u.field_data[UF_QUERY].off;
		if (used + 1 + flen >= sizeof(out->path)) {
			return -EINVAL;
		}
		out->path[used] = '?';
		memcpy(out->path + used + 1, url + off, flen);
		out->path[used + 1 + flen] = '\0';
	}

	if (out->tls && !IS_ENABLED(CONFIG_OTA_HTTP_TLS)) {
		return -ENOTSUP;
	}

	return 0;
}

static int connect_socket(const struct parsed_url *p)
{
	struct zsock_addrinfo hints = {
		.ai_family = AF_UNSPEC,
		.ai_socktype = SOCK_STREAM,
	};
	struct zsock_addrinfo *res = NULL;
	int sock = -1;
	int ret;

	ret = zsock_getaddrinfo(p->host, p->port, &hints, &res);
	if (ret != 0 || res == NULL) {
		LOG_ERR("cannot resolve %s (%d)", p->host, ret);
		return -EHOSTUNREACH;
	}

#if defined(CONFIG_OTA_HTTP_TLS)
	if (p->tls) {
		sec_tag_t tags[] = { CONFIG_OTA_HTTP_TLS_SEC_TAG };
		int verify = CONFIG_OTA_HTTP_TLS_PEER_VERIFY;

		sock = zsock_socket(res->ai_family, res->ai_socktype, IPPROTO_TLS_1_2);
		if (sock >= 0) {
			if (zsock_setsockopt(sock, SOL_TLS, TLS_SEC_TAG_LIST, tags,
					     sizeof(tags)) < 0 ||
			    zsock_setsockopt(sock, SOL_TLS, TLS_HOSTNAME, p->host,
					     strlen(p->host) + 1) < 0 ||
			    zsock_setsockopt(sock, SOL_TLS, TLS_PEER_VERIFY, &verify,
					     sizeof(verify)) < 0) {
				LOG_ERR("cannot configure TLS (%d)", errno);
				zsock_close(sock);
				sock = -1;
			}
		}

		if (verify == ZSOCK_TLS_PEER_VERIFY_NONE) {
			LOG_WRN("peer verification is off: the server is not "
				"authenticated");
		}
	} else
#endif
	{
		sock = zsock_socket(res->ai_family, res->ai_socktype, IPPROTO_TCP);
	}

	if (sock < 0) {
		LOG_ERR("cannot create socket (%d)", errno);
		zsock_freeaddrinfo(res);
		return -errno;
	}

	if (zsock_connect(sock, res->ai_addr, res->ai_addrlen) < 0) {
		LOG_ERR("cannot connect to %s:%s (%d)", p->host, p->port, errno);
		zsock_close(sock);
		zsock_freeaddrinfo(res);
		return -ECONNREFUSED;
	}

	zsock_freeaddrinfo(res);
	return sock;
}

static int on_body(struct http_response *rsp, enum http_final_call final,
		   void *user_data)
{
	struct dl_ctx *ctx = user_data;
	bool last = (final == HTTP_DATA_FINAL);
	int ret;

	ARG_UNUSED(user_data);

	if (ctx->err != 0) {
		return 0;
	}

	ctx->progress.http_status = rsp->http_status_code;

	if (rsp->http_status_code != 200) {
		LOG_ERR("server answered %d", rsp->http_status_code);
		ctx->err = -EIO;
		return 0;
	}

	if (ctx->progress.total == 0 && rsp->content_length > 0) {
		ctx->progress.total = rsp->content_length;
	}

	if (rsp->body_frag_len > 0) {
		if (!ctx->flash_ready) {
			ret = flash_img_init(&ctx->flash);
			if (ret != 0) {
				LOG_ERR("flash_img_init failed (%d)", ret);
				ctx->err = ret;
				return 0;
			}
			ctx->flash_ready = true;
		}

		ret = flash_img_buffered_write(&ctx->flash, rsp->body_frag_start,
					       rsp->body_frag_len, last);
		if (ret != 0) {
			LOG_ERR("write failed at %zu bytes (%d)",
				ctx->progress.written, ret);
			ctx->err = ret;
			return 0;
		}

		ctx->progress.written = flash_img_bytes_written(&ctx->flash);
		if (ctx->cb != NULL) {
			ctx->cb(&ctx->progress, ctx->user_data);
		}
	} else if (last && ctx->flash_ready) {
		/* Flush whatever is still buffered when the last chunk carried
		 * no body of its own.
		 */
		ret = flash_img_buffered_write(&ctx->flash, NULL, 0, true);
		if (ret != 0) {
			ctx->err = ret;
		}
	}

	return 0;
}

int ota_http_download(const char *url, ota_http_progress_cb_t cb, void *user_data)
{
	struct parsed_url p;
	struct http_request req;
	int sock;
	int ret;

	if (url == NULL) {
		return -EINVAL;
	}

	ret = parse_url(url, &p);
	if (ret != 0) {
		LOG_ERR("bad url: %s", url);
		return ret;
	}

	memset(&dl, 0, sizeof(dl));
	dl.cb = cb;
	dl.user_data = user_data;

	sock = connect_socket(&p);
	if (sock < 0) {
		return sock;
	}

	LOG_INF("downloading %s from %s:%s", p.path, p.host, p.port);

	memset(&req, 0, sizeof(req));
	req.method = HTTP_GET;
	req.url = p.path;
	req.host = p.host;
	req.protocol = "HTTP/1.1";
	req.response = on_body;
	req.recv_buf = recv_buf;
	req.recv_buf_len = sizeof(recv_buf);

	ret = http_client_req(sock, &req, CONFIG_OTA_HTTP_TIMEOUT_MS, &dl);
	zsock_close(sock);

	if (ret < 0) {
		LOG_ERR("http request failed (%d)", ret);
		return ret;
	}

	if (dl.err != 0) {
		return dl.err;
	}

	if (dl.progress.written == 0) {
		LOG_ERR("server sent no data");
		return -ENODATA;
	}

	/* No hash is verified here on purpose: MCUboot validates the image
	 * signature before swapping, and that is the check that decides
	 * whether the image ever runs.
	 */
	LOG_INF("wrote %zu bytes to slot %u", dl.progress.written,
		flash_img_get_upload_slot());

	return 0;
}

int ota_http_apply(bool permanent)
{
	int ret = boot_request_upgrade(permanent ? BOOT_UPGRADE_PERMANENT
						 : BOOT_UPGRADE_TEST);

	if (ret != 0) {
		LOG_ERR("boot_request_upgrade failed (%d)", ret);
		return ret;
	}

	LOG_INF("upgrade requested (%s)", permanent ? "permanent" : "test");
	return 0;
}

int ota_http_confirm(void)
{
	int ret = boot_write_img_confirmed();

	if (ret != 0) {
		LOG_ERR("confirm failed (%d)", ret);
		return ret;
	}

	cancel_revert_timer();
	LOG_INF("image confirmed");
	return 0;
}

bool ota_http_pending_confirm(void)
{
	return !boot_is_img_confirmed();
}

int ota_http_erase(void)
{
	int ret = boot_erase_img_bank(flash_img_get_upload_slot());

	if (ret != 0) {
		LOG_ERR("erase failed (%d)", ret);
	}

	return ret;
}

size_t ota_http_bytes_written(void)
{
	return dl.progress.written;
}

#if defined(CONFIG_OTA_HTTP_TLS)
int ota_http_tls_add_ca(const void *cert, size_t len)
{
	int ret = tls_credential_add(CONFIG_OTA_HTTP_TLS_SEC_TAG,
				     TLS_CREDENTIAL_CA_CERTIFICATE, cert, len);

	if (ret != 0 && ret != -EEXIST) {
		LOG_ERR("cannot register CA certificate (%d)", ret);
		return ret;
	}

	return 0;
}
#endif /* CONFIG_OTA_HTTP_TLS */

#if CONFIG_OTA_HTTP_CONFIRM_TIMEOUT_S > 0

#include <zephyr/sys/reboot.h>

static void revert_expired(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(revert_work, revert_expired);
static int64_t revert_deadline;

static void revert_expired(struct k_work *work)
{
	ARG_UNUSED(work);

	if (boot_is_img_confirmed()) {
		return;
	}

	LOG_ERR("image not confirmed within %d s, rebooting to revert",
		CONFIG_OTA_HTTP_CONFIRM_TIMEOUT_S);
	k_sleep(K_MSEC(100));
	sys_reboot(SYS_REBOOT_COLD);
}

static int arm_revert_timer(void)
{
	if (boot_is_img_confirmed()) {
		return 0;
	}

	revert_deadline = k_uptime_get() +
			  (CONFIG_OTA_HTTP_CONFIRM_TIMEOUT_S * MSEC_PER_SEC);

	LOG_WRN("running an unconfirmed image, %d s to confirm it",
		CONFIG_OTA_HTTP_CONFIRM_TIMEOUT_S);

	k_work_schedule(&revert_work, K_SECONDS(CONFIG_OTA_HTTP_CONFIRM_TIMEOUT_S));
	return 0;
}

SYS_INIT(arm_revert_timer, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);

uint32_t ota_http_confirm_deadline(void)
{
	int64_t left;

	if (boot_is_img_confirmed()) {
		return 0;
	}

	left = revert_deadline - k_uptime_get();
	return (left > 0) ? (uint32_t)(left / MSEC_PER_SEC) : 0;
}

static void cancel_revert_timer(void)
{
	(void)k_work_cancel_delayable(&revert_work);
}

#else /* CONFIG_OTA_HTTP_CONFIRM_TIMEOUT_S == 0 */

uint32_t ota_http_confirm_deadline(void)
{
	return 0;
}

static void cancel_revert_timer(void)
{
}

#endif /* CONFIG_OTA_HTTP_CONFIRM_TIMEOUT_S */
