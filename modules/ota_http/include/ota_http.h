/* HTTP firmware download straight into the MCUboot secondary slot.
 *
 * Point it at a URL, it streams the image into the slot that MCUboot is not
 * running from, then asks the bootloader to swap on the next reset.
 */

#ifndef OTA_HTTP_H_
#define OTA_HTTP_H_

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Progress reported while the image is being written. */
struct ota_http_progress {
	/** Bytes written to the slot so far. */
	size_t written;
	/** Total image size, or 0 when the server sent no Content-Length. */
	size_t total;
	/** HTTP status code of the response. */
	int http_status;
};

/**
 * @brief Progress callback, invoked from the calling thread.
 *
 * @param progress Current state of the transfer.
 * @param user_data Pointer handed to ota_http_download().
 */
typedef void (*ota_http_progress_cb_t)(const struct ota_http_progress *progress,
				       void *user_data);

/**
 * @brief Download a firmware image and write it to the secondary slot.
 *
 * Blocks until the whole image is written or the transfer fails. The slot is
 * erased as it is written; a failed download leaves it incomplete, so run
 * ota_http_erase() before retrying if you want a clean slate.
 *
 * This only stores the image. Call ota_http_apply() to boot it.
 *
 * @param url Absolute URL, "http://host[:port]/path" or https when
 *            CONFIG_OTA_HTTP_TLS is enabled.
 * @param cb Optional progress callback, may be NULL.
 * @param user_data Passed back to @p cb untouched.
 *
 * @retval 0 The image was written and its integrity check passed.
 * @retval -EINVAL The URL could not be parsed.
 * @retval -ENOTSUP The URL uses a scheme this build cannot handle.
 * @retval -EIO The server answered with a status other than 200.
 * @retval -errno Any socket, HTTP or flash error.
 */
int ota_http_download(const char *url, ota_http_progress_cb_t cb, void *user_data);

/**
 * @brief Ask MCUboot to boot the downloaded image on the next reset.
 *
 * @param permanent When true the image is made permanent immediately. When
 *                  false it boots once for evaluation, and the bootloader
 *                  reverts to the running image unless ota_http_confirm() is
 *                  called before the following reset.
 *
 * @retval 0 on success, negative errno otherwise.
 */
int ota_http_apply(bool permanent);

/**
 * @brief Confirm the running image so the bootloader stops reverting it.
 *
 * @retval 0 on success, negative errno otherwise.
 */
int ota_http_confirm(void);

/**
 * @brief Whether the running image still has to be confirmed.
 *
 * True right after a test upgrade, until ota_http_confirm() is called.
 */
bool ota_http_pending_confirm(void);

/**
 * @brief Erase the secondary slot.
 *
 * @retval 0 on success, negative errno otherwise.
 */
int ota_http_erase(void);

/**
 * @brief Seconds left before an unconfirmed image is reverted.
 *
 * Returns 0 when CONFIG_OTA_HTTP_CONFIRM_TIMEOUT_S is 0, when the running
 * image is already confirmed, or once the deadline has passed.
 */
uint32_t ota_http_confirm_deadline(void);

#if defined(CONFIG_OTA_HTTP_TLS) || defined(__DOXYGEN__)
/**
 * @brief Register the CA certificate used to authenticate https servers.
 *
 * Call once before the first https download. The certificate is stored under
 * CONFIG_OTA_HTTP_TLS_SEC_TAG, so it can equally be added by the application
 * with tls_credential_add() and this call skipped.
 *
 * @param cert DER or PEM certificate. Must stay valid for as long as it is
 *             used, so point it at static storage.
 * @param len Size of @p cert in bytes.
 *
 * @retval 0 on success, negative errno otherwise.
 */
int ota_http_tls_add_ca(const void *cert, size_t len);
#endif

/**
 * @brief Bytes written by the most recent download.
 */
size_t ota_http_bytes_written(void);

#ifdef __cplusplus
}
#endif

#endif /* OTA_HTTP_H_ */
