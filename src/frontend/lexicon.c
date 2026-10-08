/* license:BSD-3-Clause
 * copyright-holders:tgeczy
 *
 * lexicon.c -- the CMU Pronouncing Dictionary in front of the 1976 rules.
 *
 * The NRL rules guess every word from its spelling and guess stressed
 * vowels wrong often enough to matter ("seventy" as "SEE-ventee", "zero"
 * as "zeh-ro", "dialog" as "dee-uh-log").  For a word the dictionary knows
 * (about 125,000), its own pronunciation is used instead.  It is rewritten
 * into the stream format the rest of the pipeline already reads (Wasser's
 * phoneme names), so arpabet_to_sc01.c's NRL translation rules still pick
 * the SC-01 phones, and the dictionary's stress marks pick the SC-01's
 * short vowels for its reduced ones (AH0, IH0, IY0, UW0).  Words the
 * dictionary lacks fall through to the rules unchanged.
 *
 * The table (ttv_lex_table) is generated at build time from
 * data/cmudict.dict by build.ps1: each entry is "word\0codes", one code
 * character per phoneme-with-stress, indexing ttv_lex_symbols.
 */
#include <string.h>

extern const char *const ttv_lex_symbols[];
extern const int ttv_lex_symbol_count;
extern const char *const ttv_lex_table[];
extern const int ttv_lex_count;

static int g_enabled = 1;

void ttv_lexicon_enable(int on)
{
	g_enabled = on != 0;
}

static int code_index(char c)
{
	int u = (unsigned char)c;
	if (u >= 35 && u <= 91)
		return u - 35;
	if (u >= 93)
		return u - 93 + 57;
	return -1;
}

/* CMU consonants in the stream's spelling: single lowercase letters, with
 * j for JH and h for HH; the digraphs stay uppercase. */
static const char *consonant(const char *sym)
{
	if (!strcmp(sym, "JH")) return "j";
	if (!strcmp(sym, "HH")) return "h";
	if (sym[1] == '\0') {
		static char one[2];
		one[0] = (char)(sym[0] - 'A' + 'a');
		one[1] = '\0';
		return one;
	}
	return sym;    /* CH DH NG SH TH ZH */
}

/* Function words the dictionary marks stressed (of = AH1 V) but running
 * speech reduces: their "uh" stays the schwa, as the 1976 rules had it,
 * not the full 185 ms UH of "cup". */
static int is_weak_form(const char *word)
{
	static const char *const k_weak[] = {
		"but", "does", "from", "must", "of", "us", "what"
	};
	for (int i = 0; i < (int)(sizeof k_weak / sizeof k_weak[0]); i++)
		if (!strcmp(word, k_weak[i]))
			return 1;
	return 0;
}

/* `upper` is the word as have_letter builds it (" WORD ").  On a hit,
 * writes the word's stream (with its trailing word space) to `out` and
 * returns its length; 0 means "not in the dictionary". */
int ttv_lexicon(const char *upper, char *out, int out_max)
{
	char word[64];
	const char *codes = 0;
	int n = 0, len = 0, lo, hi, count, weak;

	if (!g_enabled || !upper || !out || out_max < 4)
		return 0;
	while (*upper == ' ')
		upper++;
	while (*upper && *upper != ' ' && n < (int)sizeof word - 1) {
		char c = *upper++;
		word[n++] = (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
	}
	word[n] = '\0';
	if (!n)
		return 0;

	lo = 0;
	hi = ttv_lex_count - 1;
	while (lo <= hi) {
		int mid = (lo + hi) / 2;
		int c = strcmp(word, ttv_lex_table[mid]);
		if (c == 0) {
			codes = ttv_lex_table[mid] + n + 1;
			break;
		}
		if (c < 0)
			hi = mid - 1;
		else
			lo = mid + 1;
	}
	if (!codes)
		return 0;

	weak = is_weak_form(word);
	count = (int)strlen(codes);
	for (int i = 0; i < count; i++) {
		int k = code_index(codes[i]);
		const char *sym, *piece;
		char vowel[3];
		if (k < 0 || k >= ttv_lex_symbol_count)
			return 0;
		sym = ttv_lex_symbols[k];
		if (strlen(sym) == 3) {
			/* vowel with stress digit: drop the digit, reduce if 0 */
			vowel[0] = sym[0];
			vowel[1] = sym[1];
			vowel[2] = '\0';
			piece = vowel;
			/* Reduce only what the dictionary itself writes as reduced:
			 * AH0 is its schwa, IH0/IY0 its short i (or, ending the
			 * word, the "-y" of "city"), UW0 a short u.  Other vowels
			 * marked 0 are full vowels that happen to be unstressed --
			 * the "log" of dialog (AO0), the "doe" of window (OW0) --
			 * and keep their quality.  AX, IX, IF and UX map in
			 * arpabet_to_sc01.c to UH2, I2, Y and U1. */
			if (!strcmp(vowel, "AH") && (sym[2] == '0' || weak))
				piece = "AX";
			else if (sym[2] == '0' && (!strcmp(vowel, "IH") || !strcmp(vowel, "IY")))
				piece = (i == count - 1) ? "IF" : "IX";
			else if (sym[2] == '0' && !strcmp(vowel, "UW"))
				piece = "UX";
		} else
			piece = consonant(sym);
		while (*piece) {
			if (len >= out_max - 2)
				return 0;
			out[len++] = *piece++;
		}
	}
	out[len++] = ' ';
	out[len] = '\0';
	return len;
}
