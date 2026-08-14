/*
 * DS45PAD - analog stick response.
 *
 * This is the part that makes the sticks behave like a DualShock 3's rather
 * than like a generic gamepad's, and it is worth explaining why they differ at
 * all, because the usual assumption - that it is a mapping bug - is wrong.
 * Both controllers report 0..255 with 0x80 at rest, and the plugin copies
 * those bytes across untouched. The difference is the response curve.
 *
 * A DualShock 3's ADC saturates well before the stick hits its mechanical
 * stop, at roughly 80% of full travel. Consequences:
 *
 *   - pushing an axis fully gives a solid 0x00 / 0xFF, with travel to spare;
 *   - a full diagonal, which puts each axis at 0.707 of mechanical travel,
 *     reads 0.707 / 0.80 = 0.88 of full scale, so about 0x0F / 0xF0.
 *
 * A DualShock 4 or DualSense is linear all the way to the stop. The same full
 * diagonal reads 0.707 of full scale - about 0x26 / 0xD9. That is a long way
 * from what a PS3 game was tuned against, which is exactly why so many of them
 * misbehave at the edges: not enough turn, no full sprint, diagonals that
 * never register as fully committed.
 *
 * Hall effect sticks make it worse. Those modules are linear over the whole
 * range with no clipping, and many of them only swing about 0x14..0xEB, so
 * they fall short even on a single axis.
 *
 * The fix is one parameter. Normalise each axis against the range that stick
 * genuinely reaches, then divide by the saturation fraction and clamp:
 *
 *     f = clamp(f_measured / 0.80, -1, +1)
 *
 * On-axis, 1.00 / 0.80 saturates - full scale reached early, exactly like a
 * DS3, and short-travel Hall sticks reach it too. On a diagonal, 0.707 / 0.80
 * gives 0.88 - the DS3 figure. Rotating the stick traces a circle clipped by
 * the clamp into a squircle with flat sides, which is the shape a real DS3
 * produces. One number reproduces the whole curve.
 */

#include <string.h>
#include <cell/cell_fs.h>

#include "ds45pad.h"

/* Fixed point: axes are normalised to +/- DS45_UNIT internally. */
#define DS45_UNIT 1024

/* Starting assumption before anything has been learned: a conservative range
 * that suits a short-travel Hall effect module. Real ranges only ever widen
 * from here, and the saturation clamp absorbs the error meanwhile. */
#define CAL_DEFAULT_MIN 20
#define CAL_DEFAULT_MAX 235
#define CAL_DEFAULT_CTR 128

/* Smallest half-span that can possibly come from a real stick. Anything under
 * this is a corrupt or half-initialised calibration, and using it would make
 * that side of the axis saturate almost immediately - so it gets thrown away
 * and rebuilt from the defaults instead. */
#define CAL_MIN_SPAN 30

/* Centre detection only runs for this many samples after the controller shows
 * up - about two seconds - and is frozen afterwards.
 *
 * An earlier version kept adjusting the centre for as long as the plugin ran,
 * accepting any value within 25 units of 0x80 that held still for a second.
 * That is exactly what a stick looks like when you hold it slightly tilted
 * while playing, so the centre drifted to wherever you had been holding it and
 * the pad then reported a permanent offset at rest - a cursor that scrolls the
 * XMB by itself. Learning the centre is only safe while the stick can be
 * assumed untouched. */
#define CAL_CENTRE_WINDOW_SAMPLES 500
/* And the value still has to be close to 0x80: if the stick happens to be held
 * during those two seconds, keeping 0x80 is better than believing it. */
#define CAL_REST_WINDOW 10
/* Samples the value must hold still before it is accepted. */
#define CAL_REST_SAMPLES 200

static uint32_t save_countdown;

static void axis_defaults(ds45_axis_cal_t *a)
{
	a->min = CAL_DEFAULT_MIN;
	a->max = CAL_DEFAULT_MAX;
	a->ctr = CAL_DEFAULT_CTR;
	a->rest_cand = CAL_DEFAULT_CTR;
	a->rest_hits = 0;
	a->centre_budget = CAL_CENTRE_WINDOW_SAMPLES;
}

/* Both half-spans have to be wide enough to be believable. */
static int axis_sane(const ds45_axis_cal_t *a)
{
	return (int)a->ctr - (int)a->min >= CAL_MIN_SPAN &&
	       (int)a->max - (int)a->ctr >= CAL_MIN_SPAN;
}

void sticks_cal_defaults(ds45_stickcal_t *cal)
{
	int i;

	memset(cal, 0, sizeof(*cal));

	for (i = 0; i < DS45_AXIS_NUM; i++)
		axis_defaults(&cal->axis[i]);
}

/* ------------------------------------------------------------------------ */
/* Learning                                                                  */
/* ------------------------------------------------------------------------ */

/* Extremes widen by one unit per sample. A stick genuinely held at its stop
 * converges in a fraction of a second; a single noise spike can only ever move
 * the calibration by one, which is below the resolution anyone can feel. */
static void learn(ds45_axis_cal_t *c, uint8_t raw, int *dirty)
{
	if (raw < c->min) {
		c->min--;
		*dirty = 1;
	} else if (raw > c->max) {
		c->max++;
		*dirty = 1;
	}

	/* Centre tracking, for sticks that do not rest exactly at 0x80. Runs only
	 * during the opening window, while the stick can still be assumed to be
	 * untouched, then stops for good. */
	if (c->centre_budget > 0) {
		c->centre_budget--;

		if (raw >= CAL_DEFAULT_CTR - CAL_REST_WINDOW &&
		    raw <= CAL_DEFAULT_CTR + CAL_REST_WINDOW) {
			if (raw + 1 >= c->rest_cand && raw <= c->rest_cand + 1) {
				if (++c->rest_hits >= CAL_REST_SAMPLES) {
					c->rest_hits = 0;
					c->centre_budget = 0;   /* settled - stop looking */
					if (c->ctr != c->rest_cand) {
						c->ctr = c->rest_cand;
						*dirty = 1;
					}
				}
			} else {
				c->rest_cand = raw;
				c->rest_hits = 0;
			}
		} else {
			c->rest_hits = 0;
		}
	}

	/* Last line of defence. Nudging the ends apart by one, as an earlier
	 * version did, produced a span of a single unit and made that half of the
	 * axis behave like a switch - so a degenerate calibration is discarded
	 * outright and relearned instead. */
	if (!axis_sane(c)) {
		axis_defaults(c);
		*dirty = 1;
	}
}

/* ------------------------------------------------------------------------ */
/* Transform                                                                 */
/* ------------------------------------------------------------------------ */

static int clampi(int v, int lo, int hi)
{
	if (v < lo) return lo;
	if (v > hi) return hi;
	return v;
}

/* Bit-by-bit integer square root. The deadzone has to be radial to feel right,
 * and an approximation would show up as the stick reaching the deadzone edge
 * at different distances depending on direction. */
static uint32_t isqrt32(uint32_t n)
{
	uint32_t rem = 0, root = 0;
	int i;

	for (i = 0; i < 16; i++) {
		root <<= 1;
		rem = (rem << 2) | (n >> 30);
		n <<= 2;
		if (root < rem) {
			root++;
			rem -= root;
			root++;
		}
	}

	return root >> 1;
}

/* Raw byte -> +/- DS45_UNIT, using the two half-spans separately so an
 * off-centre stick still reaches full scale on both sides. */
static int normalise(uint8_t raw, const ds45_axis_cal_t *c)
{
	int v = (int)raw - (int)c->ctr;
	int span;

	if (v >= 0) {
		span = (int)c->max - (int)c->ctr;
		if (span < 1)
			span = 1;
	} else {
		span = (int)c->ctr - (int)c->min;
		if (span < 1)
			span = 1;
	}

	return clampi((v * DS45_UNIT) / span, -DS45_UNIT, DS45_UNIT);
}

/* Radial deadzone that rescales the remainder, so the usable range stays
 * linear from just outside the deadzone all the way to full scale instead of
 * having a step cut out of the middle. */
static void deadzone(int *x, int *y, int dz)
{
	uint32_t mag;
	int scaled;

	if (dz <= 0)
		return;

	mag = isqrt32((uint32_t)((*x) * (*x) + (*y) * (*y)));

	if ((int)mag <= dz) {
		*x = 0;
		*y = 0;
		return;
	}

	scaled = (int)(((mag - (uint32_t)dz) * DS45_UNIT) /
	               (uint32_t)(DS45_UNIT - dz));

	*x = (int)(((long)*x * scaled) / (long)mag);
	*y = (int)(((long)*y * scaled) / (long)mag);
}

/* +/- DS45_UNIT -> 0..255 with 0x80 at rest. The two sides use different
 * scales because 0x80 is not the midpoint of 0..255: there are 128 steps
 * below it and 127 above. */
static uint8_t to_byte(int v)
{
	int out;

	if (v < 0)
		out = 128 + (v * 128) / DS45_UNIT;
	else
		out = 128 + (v * 127) / DS45_UNIT;

	return (uint8_t)clampi(out, 0, 255);
}

static void one_stick(ds45_pad_t *pad, int ax, int ay,
                      uint8_t raw_x, uint8_t raw_y, int learnable,
                      uint8_t *out_x, uint8_t *out_y)
{
	int x, y, sat, dz;

	if (g_cfg.stick_calibrate && learnable) {
		learn(&pad->cal.axis[ax], raw_x, &pad->cal.dirty);
		learn(&pad->cal.axis[ay], raw_y, &pad->cal.dirty);
	}

	x = normalise(raw_x, &pad->cal.axis[ax]);
	y = normalise(raw_y, &pad->cal.axis[ay]);

	dz = (int)(DS45_UNIT * g_cfg.stick_deadzone) / 100;
	deadzone(&x, &y, dz);

	/* The DualShock 3 curve. Per axis, not radial: scaling the magnitude
	 * would shrink the diagonals instead of extending them, which is the
	 * opposite of what is needed. */
	sat = (int)g_cfg.stick_saturation;
	if (sat > 0 && sat != 100) {
		x = clampi((x * 100) / sat, -DS45_UNIT, DS45_UNIT);
		y = clampi((y * 100) / sat, -DS45_UNIT, DS45_UNIT);
	}

	*out_x = to_byte(x);
	*out_y = to_byte(y);
}

void sticks_transform(ds45_pad_t *pad, const ds45_state_t *st,
                      uint8_t *lx, uint8_t *ly, uint8_t *rx, uint8_t *ry)
{
	/* Only a report that genuinely carried stick data may teach the
	 * calibration; see the note in passthru.c on unpopulated reports. */
	int learnable = st->sticks_valid ? 1 : 0;

	one_stick(pad, DS45_AXIS_LX, DS45_AXIS_LY, st->lx, st->ly, learnable, lx, ly);
	one_stick(pad, DS45_AXIS_RX, DS45_AXIS_RY, st->rx, st->ry, learnable, rx, ry);
}

/* ------------------------------------------------------------------------ */
/* Persistence                                                               */
/*                                                                           */
/* Learning takes under a second of stick movement, but saving it means the   */
/* very first menu after a reboot is already correct.                        */
/* ------------------------------------------------------------------------ */

static const char axis_tag[DS45_AXIS_NUM] = { 'A', 'B', 'C', 'D' };

void sticks_load(void)
{
	static char text[256];
	ds45_stickcal_t cal;
	int len, i, p;

	sticks_cal_defaults(&cal);

	len = ds45_read_file(DS45_CAL_FILE, text, sizeof(text) - 1);
	if (len > 0) {
		text[len] = 0;

		/* One line per axis: "<tag> <min> <ctr> <max>". Terse on purpose -
		 * this file is written by the plugin, not edited by hand. */
		for (p = 0; p + 3 < len; p++) {
			int v[3], n = 0, k;

			for (i = 0; i < DS45_AXIS_NUM; i++)
				if (text[p] == axis_tag[i])
					break;
			if (i == DS45_AXIS_NUM)
				continue;

			k = p + 1;
			while (n < 3 && k < len) {
				while (k < len && (text[k] == ' ' || text[k] == '\t'))
					k++;
				if (k >= len || text[k] < '0' || text[k] > '9')
					break;
				v[n] = 0;
				while (k < len && text[k] >= '0' && text[k] <= '9')
					v[n] = v[n] * 10 + (text[k++] - '0');
				n++;
			}

			if (n == 3 && v[0] < v[1] && v[1] < v[2] && v[2] <= 255) {
				ds45_axis_cal_t a;

				a.min = (uint8_t)v[0];
				a.ctr = (uint8_t)v[1];
				a.max = (uint8_t)v[2];
				a.rest_cand = (uint8_t)v[1];
				a.rest_hits = 0;
				/* A centre that came from disk was already settled once;
				 * do not reopen the question. */
				a.centre_budget = 0;

				/* A saved file from a buggy build can be degenerate; ignore
				 * it rather than start the session with a broken axis. */
				if (axis_sane(&a))
					cal.axis[i] = a;
			}
		}
	}

	for (i = 0; i < DS45_MAX_PADS; i++)
		g_pads[i].cal = cal;
}

static int put_dec(char *buf, int v)
{
	char tmp[4];
	int n = 0, i = 0;

	do {
		tmp[n++] = (char)('0' + (v % 10));
		v /= 10;
	} while (v && n < 3);

	while (n)
		buf[i++] = tmp[--n];

	return i;
}

void sticks_save_if_dirty(int force)
{
	char text[128];
	ds45_pad_t *src = NULL;
	int i, n = 0, fd;
	uint64_t written;

	if (save_countdown && !force) {
		save_countdown--;
		return;
	}

	for (i = 0; i < DS45_MAX_PADS; i++) {
		if (g_pads[i].in_use && g_pads[i].cal.dirty) {
			src = &g_pads[i];
			break;
		}
	}

	if (!src)
		return;

	for (i = 0; i < DS45_AXIS_NUM; i++) {
		text[n++] = axis_tag[i];
		text[n++] = ' ';
		n += put_dec(&text[n], src->cal.axis[i].min);
		text[n++] = ' ';
		n += put_dec(&text[n], src->cal.axis[i].ctr);
		text[n++] = ' ';
		n += put_dec(&text[n], src->cal.axis[i].max);
		text[n++] = '\n';
	}

	if (cellFsOpen(DS45_CAL_FILE, CELL_FS_O_WRONLY | CELL_FS_O_CREAT |
	               CELL_FS_O_TRUNC, &fd, NULL, 0) == CELL_FS_SUCCEEDED) {
		cellFsWrite(fd, text, (uint64_t)n, &written);
		cellFsClose(fd);
		src->cal.dirty = 0;
	}

	/* Whether or not that worked, back off before trying again. The caller
	 * ticks this once a second, so the file is touched at most every half a
	 * minute even while the sticks are still being learned. */
	save_countdown = 30;
}
