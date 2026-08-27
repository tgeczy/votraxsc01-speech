/* license:BSD-3-Clause
 * copyright-holders:tgeczy
 *
 * text_to_votrax.c -- English text to SC-01 phoneme codes: the public
 * face of the two-stage NRL pipeline.
 *
 *   stage 1  wasser_parse.c + third_party/wasser/   text -> phonemes
 *            (the NRL letter-to-sound rules, John A. Wasser's public
 *            domain 1985 C implementation, rules files byte-identical)
 *   stage 2  arpabet_to_sc01.c                      phonemes -> codes
 *            (NRL Report 7948's own IPA-to-Votrax translation rules)
 *
 * One C implementation serves every consumer -- the NVDA add-on, the
 * SAPI engine and the say01 probe -- so pronunciation can never drift
 * between them.
 *
 * Thread contract: NOT reentrant (the 1985 engine underneath is built
 * on globals).  Every consumer already serializes: NVDA calls from its
 * main thread, the SAPI engine holds its Speak mutex, say01 is
 * single-threaded.
 */
#include "text_to_votrax.h"

#include <stdlib.h>

extern int wasser_translate(const char *text, char *out, int out_max);
extern int wasser_spell(const char *text, char *out, int out_max);
extern int map_arpabet_to_sc01(const char *arpa, uint8_t *phones, int max_phones);

/* The Wasser stream is wordier than the text (phoneme names, spelled
 * numbers); eight bytes per input byte plus slack covers the worst case
 * ("$1.99" becomes a whole sentence). */
static int run(int (*stage1)(const char *, char *, int),
               const char *text, uint8_t *phones, int max_phones)
{
	size_t text_len, mid_max;
	char *mid;
	int n;

	if (!text || !phones || max_phones <= 0)
		return 0;
	text_len = 0;
	while (text[text_len])
		text_len++;
	mid_max = text_len * 8 + 256;
	mid = (char *)malloc(mid_max);
	if (!mid)
		return 0;
	stage1(text, mid, (int)mid_max);
	n = map_arpabet_to_sc01(mid, phones, max_phones);
	free(mid);
	return n;
}

int ttv_translate(const char *text, uint8_t *phones, int max_phones)
{
	return run(wasser_translate, text, phones, max_phones);
}

int ttv_spell(const char *text, uint8_t *phones, int max_phones)
{
	return run(wasser_spell, text, phones, max_phones);
}
