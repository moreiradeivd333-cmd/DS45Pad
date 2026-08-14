/*
 * DS45PAD - diagnostics.
 *
 * The Bluetooth passthrough path has one genuinely unknown: what, if
 * anything, the firmware's generic HID driver does with the buttons a
 * DualShock 3 does not have - PS, touchpad click, mute. That cannot be
 * answered from documentation, only measured on the console.
 *
 * Turning DIAG on writes every pad code that changes to
 * /dev_hdd0/tmp/ds45pad_diag.txt, so pressing a button and reading the file
 * tells you exactly which code and bit to put in BT_PS_CODE / BT_PS_MASK.
 *
 * cellPadGetRawData() would be worth sampling too, since it can carry codes
 * the cooked path drops, but SDK 4.75 neither declares it nor exports it from
 * libio_stub, so there is nothing to link against.
 */

#include <string.h>
#include <cell/cell_fs.h>
#include <cell/pad.h>

#include "ds45pad.h"

/* Writes are buffered and only committed when the buffer is nearly full.
 *
 * The first version opened, wrote and closed the file on every change, from a
 * thread running at 250 Hz. That competes with the VSH for disk I/O hard
 * enough to stop multiMAN from loading at all, which is a poor trade for a
 * debug log. The total is capped too, so leaving DIAG on cannot fill the
 * drive or slow the console down indefinitely. */
#define DIAG_BUF       4096
#define DIAG_FLUSH_AT  3072
#define DIAG_MAX_BYTES (64 * 1024)

static char diag_buf[DIAG_BUF];
static int  diag_len;
static int  diag_written;
static int  diag_full;

static uint16_t last_cooked[DS45_MAX_PORT_NUM][DS45_PAD_LEN];
static int      seeded[DS45_MAX_PORT_NUM];

static void commit(void)
{
	int fd;
	uint64_t written;

	if (diag_len <= 0)
		return;

	if (cellFsOpen(DS45_DIAG_FILE,
	               CELL_FS_O_WRONLY | CELL_FS_O_CREAT | CELL_FS_O_APPEND,
	               &fd, NULL, 0) == CELL_FS_SUCCEEDED) {
		cellFsWrite(fd, diag_buf, (uint64_t)diag_len, &written);
		cellFsClose(fd);
		diag_written += diag_len;
		if (diag_written >= DIAG_MAX_BYTES)
			diag_full = 1;
	}

	diag_len = 0;
}

/* Only commits when the buffer is nearly full; callers no longer force it. */
static void flush(void)
{
	if (diag_len >= DIAG_FLUSH_AT)
		commit();
}

static void put(const char *s)
{
	if (diag_full)
		return;

	while (*s) {
		if (diag_len >= DIAG_BUF - 1)
			commit();
		diag_buf[diag_len++] = *s++;
	}
}

static void put_hex(uint32_t v, int digits)
{
	static const char hex[] = "0123456789ABCDEF";
	char tmp[9];
	int i;

	if (digits > 8) digits = 8;
	tmp[digits] = 0;
	for (i = digits - 1; i >= 0; i--) {
		tmp[i] = hex[v & 0xF];
		v >>= 4;
	}

	put(tmp);
}

static void put_dec(int v)
{
	char tmp[12];
	int i = 11;

	tmp[i] = 0;
	if (v < 0) {
		put("-");
		v = -v;
	}

	do {
		tmp[--i] = (char)('0' + (v % 10));
		v /= 10;
	} while (v && i > 0);

	put(&tmp[i]);
}

void diag_msg(const char *msg)
{
	put(msg);
	put("\n");
	commit();     /* one-off messages are worth writing straight away */
}

void diag_dump_ports(void)
{
	CellPadInfo2 info;
	int port;

	if (cellPadGetInfo2(&info) != CELL_PAD_OK) {
		diag_msg("cellPadGetInfo2 falhou");
		return;
	}

	put("--- portas ---\n");
	put("max="); put_dec((int)info.max_connect);
	put(" conectados="); put_dec((int)info.now_connect);
	put("\n");

	for (port = 0; port < DS45_MAX_PORT_NUM; port++) {
		if (!(info.port_status[port] & DS45_STATUS_CONNECTED))
			continue;

		put("porta "); put_dec(port);
		put("  status=0x");   put_hex(info.port_status[port], 4);
		put("  setting=0x");  put_hex(info.port_setting[port], 4);
		put("  cap=0x");      put_hex(info.device_capability[port], 4);
		put("  type=");       put_dec((int)info.device_type[port]);
		put(info.device_capability[port] & DS45_CAP_PS3_CONFORMITY
		    ? "  [CONFORME]" : "  [GENERICO]");
		put("\n");
	}

	put("\n");
	commit();
}

static void dump_changes(const char *tag, int port, const CellPadData *data,
                         uint16_t *last, int seed)
{
	int code, changed = 0;

	for (code = 0; code < DS45_PAD_LEN && code < data->len; code++) {
		uint16_t v = data->button[code];

		if (v == last[code])
			continue;

		last[code] = v;
		if (seed)
			continue;

		if (!changed) {
			put(tag); put(" porta "); put_dec(port); put(":");
			changed = 1;
		}

		put("  [");   put_dec(code);
		put("]=0x");  put_hex(v, 4);
	}

	if (changed) {
		put("\n");
		flush();
	}
}

/* What the plugin itself decoded from the controller, before any of it reaches
 * libpad. This is the only way to tell "the controller never reported the
 * button" apart from "we reported it and the system ignored it". */
void diag_state(const ds45_pad_t *pad)
{
	static uint32_t last[DS45_MAX_PADS];
	static int seeded_state[DS45_MAX_PADS];
	int slot = (int)(pad - g_pads);
	uint32_t b;

	if (slot < 0 || slot >= DS45_MAX_PADS || !pad->state.valid)
		return;

	b = pad->state.buttons;
	if (seeded_state[slot] && b == last[slot])
		return;

	last[slot] = b;
	seeded_state[slot] = 1;

	put("btn slot "); put_dec(slot);
	put(" = 0x"); put_hex(b, 8);
	if (b & DS45_B_PS)       put("  PS");
	if (b & DS45_B_TOUCHPAD) put("  TOUCH");
	if (b & DS45_B_MUTE)     put("  MUTE");
	if (b & DS45_B_SHARE)    put("  SHARE");
	if (b & DS45_B_OPTIONS)  put("  OPTIONS");
	put("\n");
	flush();
}

void diag_watch(void)
{
	CellPadInfo2 info;
	CellPadData  data;
	int port;

	if (cellPadGetInfo2(&info) != CELL_PAD_OK)
		return;

	for (port = 0; port < DS45_MAX_PORT_NUM; port++) {
		if (!(info.port_status[port] & DS45_STATUS_CONNECTED))
			continue;

		/* Skip our own virtual pads: watching those would just echo back
		 * whatever we injected a moment ago. */
		if (info.device_type[port] == DS45_DEV_TYPE_LDD)
			continue;

		memset(&data, 0, sizeof(data));
		if (cellPadGetData((uint32_t)port, &data) == CELL_PAD_OK && data.len > 0)
			dump_changes("data", port, &data, last_cooked[port], !seeded[port]);

		seeded[port] = 1;
	}
}
