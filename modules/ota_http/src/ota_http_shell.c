#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>
#include <zephyr/sys/reboot.h>
#include <stdlib.h>
#include <string.h>

#include <ota_http.h>

struct shell_progress {
	const struct shell *sh;
	size_t last_pct;
};

static void progress_cb(const struct ota_http_progress *p, void *user_data)
{
	struct shell_progress *sp = user_data;
	size_t pct;

	if (p->total == 0) {
		return;
	}

	pct = (p->written * 100U) / p->total;
	if (pct >= sp->last_pct + 10U) {
		sp->last_pct = pct - (pct % 10U);
		shell_print(sp->sh, "  %zu%%  (%zu / %zu bytes)", pct,
			    p->written, p->total);
	}
}

static int cmd_download(const struct shell *sh, size_t argc, char **argv)
{
	struct shell_progress sp = { .sh = sh, .last_pct = 0 };
	int ret;

	shell_print(sh, "downloading %s", argv[1]);

	ret = ota_http_download(argv[1], progress_cb, &sp);
	if (ret != 0) {
		shell_error(sh, "download failed (%d)", ret);
		return ret;
	}

	shell_print(sh, "stored %zu bytes in the secondary slot",
		    ota_http_bytes_written());
	shell_print(sh, "run 'ota apply' to boot it once, or "
			"'ota apply permanent' to keep it");
	return 0;
}

static int cmd_apply(const struct shell *sh, size_t argc, char **argv)
{
	bool permanent = (argc > 1 && strcmp(argv[1], "permanent") == 0);
	int ret;

	ret = ota_http_apply(permanent);
	if (ret != 0) {
		shell_error(sh, "apply failed (%d)", ret);
		return ret;
	}

	if (permanent) {
		shell_print(sh, "image marked permanent; reboot to run it");
	} else {
		shell_print(sh, "image marked for test; reboot to run it once");
		shell_print(sh, "confirm it with 'ota confirm' before the next "
				"reboot, or the bootloader reverts");
	}

	return 0;
}

static int cmd_confirm(const struct shell *sh, size_t argc, char **argv)
{
	int ret = ota_http_confirm();

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	if (ret != 0) {
		shell_error(sh, "confirm failed (%d)", ret);
		return ret;
	}

	shell_print(sh, "image confirmed, it will not be reverted");
	return 0;
}

static int cmd_status(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	shell_print(sh, "running image: %s",
		    ota_http_pending_confirm() ? "UNCONFIRMED - will be "
						 "reverted on next reboot"
					       : "confirmed");
	shell_print(sh, "last download: %zu bytes", ota_http_bytes_written());
	return 0;
}

static int cmd_erase(const struct shell *sh, size_t argc, char **argv)
{
	int ret = ota_http_erase();

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	if (ret != 0) {
		shell_error(sh, "erase failed (%d)", ret);
		return ret;
	}

	shell_print(sh, "secondary slot erased");
	return 0;
}

static int cmd_reboot(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	shell_print(sh, "rebooting");
	k_sleep(K_MSEC(100));
	sys_reboot(SYS_REBOOT_COLD);
	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(ota_cmds,
	SHELL_CMD_ARG(download, NULL, "download <url> into the secondary slot",
		      cmd_download, 2, 0),
	SHELL_CMD_ARG(apply, NULL, "apply [permanent] - boot the new image",
		      cmd_apply, 1, 1),
	SHELL_CMD_ARG(confirm, NULL, "confirm the running image", cmd_confirm, 1, 0),
	SHELL_CMD_ARG(status, NULL, "show confirmation state", cmd_status, 1, 0),
	SHELL_CMD_ARG(erase, NULL, "erase the secondary slot", cmd_erase, 1, 0),
	SHELL_CMD_ARG(reboot, NULL, "reboot the board", cmd_reboot, 1, 0),
	SHELL_SUBCMD_SET_END
);

SHELL_CMD_REGISTER(ota, &ota_cmds, "HTTP firmware update", NULL);
