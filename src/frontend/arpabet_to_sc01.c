/* license:BSD-3-Clause (rules: US Government work, public domain)
 * copyright-holders:tgeczy
 *
 * arpabet_to_sc01.c -- the second half of the NRL pipeline: Wasser-stream
 * phonemes to SC-01 phone codes, transliterated from the "IPA TO VOTRAX
 * TRANSLATIONS RULES" of NRL Report 7948 (Elovitz, Johnson, McHugh &
 * Shore, 1976; 17 USC 105 public domain).  Source of the transcription:
 * the report's own SNOBOL listing (TRANS.SNO), checked against a second
 * transcription and the DTIC scan where legible.
 *
 * Two editorial decisions, both documented because the surviving
 * transcriptions share a common ancestor and its typos:
 *
 *   1. The rule 'L [EY]=[UH3 A1 AY' lacks its closing bracket in every
 *      transcription; the output is read as [UH3 A1 AY], consistent with
 *      the parallel non-L rule [A AY].
 *   2. 'ER [ER L]=[UH3 ER]' is read as match-ER-with-right-context-L,
 *      NOT as consuming the L: the L rule table has no ER left-context
 *      entry, so a consuming reading would delete the /l/ of "girl"
 *      outright, which no intelligibility-tested rule set would do.
 *
 * Input token conventions (the Wasser stream): two-letter uppercase
 * phonemes, single lowercase consonants ('j' = the JH affricate), spaces
 * as word boundaries, plus the clause markers wasser_parse.c emits
 * (',' '.' '-').  Normalization to NRL's names: uppercase, j -> JH,
 * NG -> NX -- the rules themselves then map NX back to the SC-01's NG
 * phone, JH to D+J, CH to T+CH, WH to H+W, HH to H, DH to THV.
 */
#include "sc01.h"

#include <string.h>

typedef struct {
	const char *left;    /* required previous source token, "" = any */
	const char *match;   /* the source token this rule consumes */
	const char *right;   /* required next source token, "" = any */
	const char *out;     /* SC-01 phone names, space separated */
} ipa_rule;

/* NRL order within each phoneme's group; first match wins. */
static const ipa_rule k_rules[] = {
	{ "",   "IY", "",   "E" },
	{ "",   "IH", "",   "I" },
	{ "L",  "EY", "R",  "UH3 A1 I3" },
	{ "L",  "EY", "",   "UH3 A1 AY" },      /* bracket typo repaired */
	{ "",   "EY", "R",  "A I3" },
	{ "",   "EY", "",   "A AY" },
	{ "L",  "EH", "",   "UH3 EH" },
	{ "",   "EH", "",   "EH" },
	{ "L",  "AE", "R",  "UH3 AE EH3" },
	{ "L",  "AE", "",   "UH3 AE" },
	{ "",   "AE", "R",  "AE1 EH3" },
	{ "",   "AE", "",   "AE" },
	{ "",   "AA", "",   "AH" },
	{ "L",  "AO", "R",  "UH3 O" },
	{ "L",  "AO", "ER", "UH3 AW O2" },
	{ "L",  "AO", "",   "UH3 AW" },
	{ "",   "AO", "R",  "O" },
	{ "",   "AO", "ER", "AW O2" },
	{ "",   "AO", "",   "AW" },
	{ "L",  "OW", "",   "UH3 O1 U1" },
	{ "",   "OW", "",   "O1 U1" },
	{ "L",  "UH", "",   "UH3 OO" },
	{ "",   "UH", "",   "OO" },
	{ "",   "UW", "",   "IU U" },
	{ "IY", "ER", "",   "I3 ER" },
	{ "ER", "ER", "",   "IU R" },
	{ "L",  "ER", "",   "UH3 ER" },
	{ "",   "ER", "L",  "UH3 ER" },         /* see decision 2 above */
	{ "R",  "ER", "",   "UH3 R" },
	{ "",   "ER", "",   "ER" },
	{ "",   "AX", "",   "UH2" },
	{ "",   "AH", "",   "UH" },
	{ "",   "AY", "L",  "AH AY" },
	{ "",   "AY", "R",  "AH I3" },
	{ "",   "AY", "ER", "AH AY" },
	{ "",   "AY", "",   "AH E1" },
	{ "",   "AW", "",   "AH O1" },
	{ "L",  "OY", "ER", "UH3 O1 AY" },
	{ "L",  "OY", "L",  "UH3 O1 AY" },
	{ "L",  "OY", "R",  "UH3 O1 EH2" },
	{ "",   "OY", "ER", "O1 AY" },
	{ "",   "OY", "L",  "O1 AY" },
	{ "",   "OY", "R",  "O1 EH2" },
	{ "",   "OY", "",   "O1 E1" },
	{ "",   "Y",  "",   "Y1" },
	{ "",   "P",  "",   "P" },
	{ "",   "B",  "",   "B" },
	{ "",   "T",  "",   "T" },
	{ "",   "D",  "",   "D" },
	{ "",   "K",  "",   "K" },
	{ "",   "G",  "",   "G" },
	{ "",   "F",  "",   "F" },
	{ "",   "V",  "",   "V" },
	{ "",   "TH", "",   "TH" },
	{ "",   "DH", "",   "THV" },
	{ "",   "S",  "",   "S" },
	{ "",   "Z",  "",   "Z" },
	{ "",   "SH", "",   "SH" },
	{ "",   "ZH", "",   "ZH" },
	{ "",   "HH", "",   "H" },
	{ "",   "CH", "",   "T CH" },
	{ "",   "JH", "",   "D J" },
	{ "",   "M",  "",   "M" },
	{ "",   "N",  "",   "N" },
	{ "",   "NX", "",   "NG" },
	{ "IY", "L",  "",   "I3 L" },
	{ "EY", "L",  "",   "I3 L" },
	{ "AY", "L",  "",   "I3 L" },
	{ "OY", "L",  "",   "I3 L" },
	{ "AE", "L",  "",   "UH3 L" },
	{ "AO", "L",  "",   "UH3 L" },
	{ "OW", "L",  "",   "UH3 L" },
	{ "",   "L",  "",   "L" },
	{ "",   "W",  "",   "W" },
	{ "",   "WH", "",   "H W" },
	{ "",   "R",  "L",  "UH3 R" },
	{ "",   "R",  "",   "R" },
	/* Punctuation, per the report's PUNCTRULE set. */
	{ "",   "_",  "",   "PA0" },
	{ "",   ",",  "",   "PA1" },
	{ "",   ".",  "",   "PA1 PA1" },
	{ "",   "-",  "",   "PA1" },
};

#define N_RULES ((int)(sizeof k_rules / sizeof k_rules[0]))
#define MAX_TOKENS 1024

/* Tokenize the Wasser stream into NRL phoneme names (2 chars max). */
static int tokenize(const char *s, char toks[][3])
{
	int n = 0;
	while (*s && n < MAX_TOKENS) {
		char c = *s;
		if (c >= 'a' && c <= 'z') {
			/* Single-letter consonants.  Two spellings differ from the
			 * NRL names: 'j' is the JH affricate, and 'h' is HH -- the
			 * rules files use lowercase h throughout, whatever the
			 * header comment claims (measured: zero "HH" in english.c). */
			toks[n][0] = (c == 'j') ? 'J' : (c == 'h') ? 'H' : (char)(c - 'a' + 'A');
			toks[n][1] = (c == 'j' || c == 'h') ? 'H' : '\0';
			toks[n][2] = '\0';
			n++;
			s++;
		} else if (c >= 'A' && c <= 'Z') {
			/* two-letter uppercase phoneme */
			if (s[1] >= 'A' && s[1] <= 'Z') {
				toks[n][0] = c; toks[n][1] = s[1]; toks[n][2] = '\0';
				/* NRL spells the velar nasal NX */
				if (c == 'N' && s[1] == 'G') {
					toks[n][0] = 'N'; toks[n][1] = 'X';
				}
				n++;
				s += 2;
			} else {
				/* stray single capital: take it as itself */
				toks[n][0] = c; toks[n][1] = '\0'; toks[n][2] = '\0';
				n++;
				s++;
			}
		} else if (c == ' ') {
			/* collapse runs of word boundaries */
			if (n > 0 && !(toks[n - 1][0] == '_' && toks[n - 1][1] == '\0')) {
				toks[n][0] = '_'; toks[n][1] = '\0'; toks[n][2] = '\0';
				n++;
			}
			s++;
		} else if (c == ',' || c == '.' || c == '-') {
			/* clause markers may absorb a preceding word boundary */
			if (n > 0 && toks[n - 1][0] == '_' && toks[n - 1][1] == '\0')
				n--;
			toks[n][0] = c; toks[n][1] = '\0'; toks[n][2] = '\0';
			n++;
			s++;
		} else
			s++;
	}
	return n;
}

static int emit_names(const char *names, uint8_t *phones, int max, int len)
{
	while (*names && len < max) {
		char name[4];
		int i = 0;
		while (*names == ' ')
			names++;
		while (*names && *names != ' ' && i < 3)
			name[i++] = *names++;
		name[i] = '\0';
		if (i) {
			int code = vx_phone_by_name(name);
			if (code >= 0)
				phones[len++] = (uint8_t)code;
		}
	}
	return len;
}

int map_arpabet_to_sc01(const char *arpa, uint8_t *phones, int max_phones)
{
	static char toks[MAX_TOKENS][3];
	int n = tokenize(arpa, toks);
	int len = 0;

	for (int i = 0; i < n; i++) {
		for (int r = 0; r < N_RULES; r++) {
			const ipa_rule *rule = &k_rules[r];
			if (strcmp(rule->match, toks[i]))
				continue;
			if (rule->left[0] && (i == 0 || strcmp(rule->left, toks[i - 1])))
				continue;
			if (rule->right[0] && (i + 1 >= n || strcmp(rule->right, toks[i + 1])))
				continue;
			/* suppress a leading pause: nothing to pause after yet */
			if (len == 0 && (toks[i][0] == '_' || toks[i][0] == ',' ||
			                 toks[i][0] == '.' || toks[i][0] == '-'))
				break;
			len = emit_names(rule->out, phones, max_phones, len);
			break;
		}
	}
	return len;
}
