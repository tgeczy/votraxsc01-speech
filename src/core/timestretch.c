/* license:BSD-3-Clause
 * copyright-holders:tgeczy
 *
 * timestretch.c -- SOLA (synchronized overlap-add) time scaling.
 *
 * The scheme, in one paragraph: output is assembled in steps of
 * HOP = FRAME - OVERLAP samples.  Each step takes a FRAME-long window
 * from the input at the analysis position (nudged by up to +/-SEARCH
 * samples to the offset that best correlates with the previous step's
 * saved tail), crossfades the first OVERLAP samples against that tail,
 * emits HOP samples, saves the new tail, and advances the analysis
 * position by HOP * speed.  Pitch is untouched because every emitted
 * sample is a literal input sample; only how far the read head jumps
 * between steps changes.
 */
#include "timestretch.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

/* Tuned at the chip's 40 kHz: 25.6 ms frames, 6.4 ms crossfade, 4 ms
 * alignment search.  Speech-safe values; nothing here assumes the rate
 * beyond "tens of kilohertz". */
#define FRAME   1024
#define OVERLAP 256
#define HOP     (FRAME - OVERLAP)
#define SEARCH  160

struct vx_stretch {
	double speed;
	int16_t *in;              /* growable input FIFO */
	int in_len, in_cap;
	double ana_pos;           /* analysis read head into in[] */
	int16_t tail[OVERLAP];    /* previous step's continuation */
	int primed;               /* tail holds real audio */
	int16_t *out;             /* growable output FIFO */
	int out_len, out_cap, out_read;
};

vx_stretch *vxs_create(void)
{
	vx_stretch *s = (vx_stretch *)calloc(1, sizeof *s);
	if (s)
		s->speed = 1.0;
	return s;
}

void vxs_destroy(vx_stretch *s)
{
	if (!s)
		return;
	free(s->in);
	free(s->out);
	free(s);
}

void vxs_reset(vx_stretch *s)
{
	s->in_len = 0;
	s->ana_pos = 0;
	s->primed = 0;
	s->out_len = s->out_read = 0;
}

void vxs_set_speed(vx_stretch *s, double speed)
{
	if (speed < 0.4)
		speed = 0.4;
	if (speed > 3.0)
		speed = 3.0;
	s->speed = speed;
}

static void ensure(int16_t **buf, int *cap, int need)
{
	if (*cap >= need)
		return;
	int c = *cap ? *cap : 8192;
	while (c < need)
		c *= 2;
	*buf = (int16_t *)realloc(*buf, (size_t)c * sizeof(int16_t));
	*cap = c;
}

static void emit(vx_stretch *s, const int16_t *data, int count)
{
	ensure(&s->out, &s->out_cap, s->out_len + count);
	memcpy(s->out + s->out_len, data, (size_t)count * sizeof(int16_t));
	s->out_len += count;
}

/* Best alignment of in[base+k .. base+k+OVERLAP) against the saved tail. */
static int align(const vx_stretch *s, int base)
{
	if (!s->primed)
		return 0;
	int kmin = base >= SEARCH ? -SEARCH : -base;
	int best_k = 0;
	long long best = -1;
	for (int k = kmin; k <= SEARCH; k++) {
		long long corr = 0;
		const int16_t *p = s->in + base + k;
		for (int i = 0; i < OVERLAP; i++)
			corr += (long long)s->tail[i] * p[i];
		if (corr > best) {
			best = corr;
			best_k = k;
		}
	}
	return best_k;
}

static void process(vx_stretch *s)
{
	/* Near unity there is nothing to do better than a copy. */
	if (fabs(s->speed - 1.0) < 0.02) {
		int base = (int)s->ana_pos;
		if (base < 0)
			base = 0;
		if (base < s->in_len)
			emit(s, s->in + base, s->in_len - base);
		s->in_len = 0;
		s->ana_pos = 0;
		s->primed = 0;
		return;
	}

	for (;;) {
		int base = (int)s->ana_pos;
		if (base + SEARCH + FRAME > s->in_len)
			break;
		int k = align(s, base);
		const int16_t *frame = s->in + base + k;
		int16_t step[HOP];
		for (int i = 0; i < OVERLAP; i++)
			step[i] = (int16_t)(((int)s->tail[i] * (OVERLAP - i) +
			                     (int)frame[i] * i) / OVERLAP);
		memcpy(step + OVERLAP, frame + OVERLAP,
		       (size_t)(HOP - OVERLAP) * sizeof(int16_t));
		emit(s, step, HOP);
		memcpy(s->tail, frame + HOP, sizeof s->tail);
		s->primed = 1;
		s->ana_pos += HOP * s->speed;
	}

	/* Trim consumed input so the FIFO stays bounded. */
	int keep_from = (int)s->ana_pos - SEARCH;
	if (keep_from > FRAME) {
		memmove(s->in, s->in + keep_from,
		        (size_t)(s->in_len - keep_from) * sizeof(int16_t));
		s->in_len -= keep_from;
		s->ana_pos -= keep_from;
	}
}

void vxs_feed(vx_stretch *s, const int16_t *in, int count)
{
	if (count <= 0)
		return;
	ensure(&s->in, &s->in_cap, s->in_len + count);
	memcpy(s->in + s->in_len, in, (size_t)count * sizeof(int16_t));
	s->in_len += count;
	process(s);
}

int vxs_pull(vx_stretch *s, int16_t *out, int max)
{
	int avail = s->out_len - s->out_read;
	int n = avail < max ? avail : max;
	if (n > 0) {
		memcpy(out, s->out + s->out_read, (size_t)n * sizeof(int16_t));
		s->out_read += n;
		if (s->out_read == s->out_len)
			s->out_len = s->out_read = 0;
	}
	return n > 0 ? n : 0;
}

int vxs_stretch_buffer(const int16_t *in, int count, double speed,
                       int16_t *out, int max)
{
	vx_stretch *s = vxs_create();
	if (!s)
		return 0;
	vxs_set_speed(s, speed);
	vxs_feed(s, in, count);
	/* Whatever the analysis window could not consume is a sub-frame
	 * tail; emit it raw so the utterance never loses its ending. */
	{
		int base = (int)s->ana_pos;
		if (base < 0)
			base = 0;
		if (base < s->in_len)
			emit(s, s->in + base, s->in_len - base);
	}
	int n = vxs_pull(s, out, max);
	vxs_destroy(s);
	return n;
}
