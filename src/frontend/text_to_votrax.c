/* license:BSD-3-Clause
 * copyright-holders:tgeczy
 *
 * text_to_votrax.c -- English text to SC-01 phoneme codes.
 *
 * CURRENT STATE: the letter-to-sound section below is a PLACEHOLDER --
 * a one-letter-one-phone map that produces intelligible-but-crude speech,
 * so the whole pipeline (drivers, feeding loops, audio) can be built and
 * heard end to end.  It is being replaced by the NRL letter-to-sound rules
 * (NRL Report 7948, 1976, public domain); everything above and below that
 * section is permanent.
 *
 * Spelling is routed through ordinary words on purpose: the letter "w" is
 * translated as the text "double you".  Once the rules land, spelled
 * speech inherits their full quality with no second table to maintain.
 */
#include "text_to_votrax.h"

#include <string.h>
#include <ctype.h>

/* SC-01 phone codes, named for readability. */
enum {
	EH3 = 0x00, EH2, EH1, PA0, DT, A1, A2, ZH,
	AH2, I3, I2, I1, M, N, B, V,
	CH, SH, Z, AW1, NG, AH1, OO1, OO,
	L, K, J, H, G, F, D, S,
	A, AY, Y1, UH3, AH, P, O, I,
	U, Y, T, R, E, W, AE, AE1,
	AW2, UH2, UH1, UH, O2, O1, IU, U1,
	THV, TH, ER, EH, E1, AW, PA1, STOP
};

typedef struct {
	uint8_t *out;
	int max;
	int count;
} sink;

static void emit(sink *s, uint8_t phone)
{
	if (s->count < s->max)
		s->out[s->count++] = phone;
}

/* ---------------------------------------------------------------------
 * PLACEHOLDER letter-to-sound: one phone (or two) per letter.
 * ------------------------------------------------------------------- */
static void placeholder_letter(sink *s, char c)
{
	switch (tolower((unsigned char)c)) {
	case 'a': emit(s, AE1); break;
	case 'b': emit(s, B); break;
	case 'c': emit(s, K); break;
	case 'd': emit(s, D); break;
	case 'e': emit(s, EH1); break;
	case 'f': emit(s, F); break;
	case 'g': emit(s, G); break;
	case 'h': emit(s, H); break;
	case 'i': emit(s, I1); break;
	case 'j': emit(s, J); break;
	case 'k': emit(s, K); break;
	case 'l': emit(s, L); break;
	case 'm': emit(s, M); break;
	case 'n': emit(s, N); break;
	case 'o': emit(s, AH1); break;
	case 'p': emit(s, P); break;
	case 'q': emit(s, K); emit(s, W); break;
	case 'r': emit(s, R); break;
	case 's': emit(s, S); break;
	case 't': emit(s, T); break;
	case 'u': emit(s, UH1); break;
	case 'v': emit(s, V); break;
	case 'w': emit(s, W); break;
	case 'x': emit(s, K); emit(s, S); break;
	case 'y': emit(s, Y1); break;
	case 'z': emit(s, Z); break;
	default: break;
	}
}

/* Digits and letters as English words, for spelling and number fallback.
 * These tables are permanent; they feed ttv_translate like any text. */
static const char *digit_word(char d)
{
	static const char *words[10] = {
		"zero", "one", "two", "three", "four",
		"five", "six", "seven", "eight", "nine"
	};
	return (d >= '0' && d <= '9') ? words[d - '0'] : 0;
}

static const char *letter_word(char c)
{
	static const char *words[26] = {
		"ay", "bee", "see", "dee", "ee", "ef", "jee", "aitch",
		"eye", "jay", "kay", "el", "em", "en", "oh", "pee",
		"cue", "are", "ess", "tee", "you", "vee", "double you",
		"ex", "why", "zee"
	};
	c = (char)tolower((unsigned char)c);
	return (c >= 'a' && c <= 'z') ? words[c - 'a'] : 0;
}

static void translate_run(sink *s, const char *text)
{
	int spoke_since_pause = 0;
	for (const char *p = text; *p; p++) {
		unsigned char c = (unsigned char)*p;
		if (isalpha(c)) {
			placeholder_letter(s, (char)c);
			spoke_since_pause = 1;
		} else if (isdigit(c)) {
			const char *w = digit_word((char)c);
			if (w)
				translate_run(s, w);
			spoke_since_pause = 1;
		} else if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
			if (spoke_since_pause)
				emit(s, PA0);
			spoke_since_pause = 0;
		} else if (c == '.' || c == '!' || c == '?' || c == ',' ||
		           c == ';' || c == ':') {
			emit(s, PA1);
			spoke_since_pause = 0;
		}
		/* Everything else (symbols, non-ASCII bytes) is skipped here;
		 * pronounceable symbol names are the driver layer's business. */
	}
}

int ttv_translate(const char *text, uint8_t *phones, int max_phones)
{
	sink s = { phones, max_phones, 0 };
	if (text)
		translate_run(&s, text);
	return s.count;
}

int ttv_spell(const char *text, uint8_t *phones, int max_phones)
{
	sink s = { phones, max_phones, 0 };
	if (!text)
		return 0;
	for (const char *p = *text ? text : ""; *p; p++) {
		const char *w = 0;
		if (isalpha((unsigned char)*p))
			w = letter_word(*p);
		else if (isdigit((unsigned char)*p))
			w = digit_word(*p);
		if (w) {
			translate_run(&s, w);
			emit(&s, PA0);
		}
	}
	return s.count;
}
