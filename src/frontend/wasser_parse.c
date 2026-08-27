/* license: public domain, following its source (see below)
 *
 * wasser_parse.c -- string-driven adaptation of PARSE.C from John A.
 * Wasser's "English to Phoneme translation" (net.sources, 15 April 1985;
 * public domain, in the author's own words: "this is all public domain
 * and I make no copyright claims on it").
 *
 * The four sibling files -- english.c, phoneme.c, saynum.c, spellword.c,
 * in third_party/wasser/ -- are vendored BYTE-IDENTICAL to the USENIX
 * 1987 tape.  This file replaces only parse.c, which owned main() and
 * FILE I/O: input now comes from a string, output lands in a caller's
 * buffer, and the word-assembly logic (ordinals, decimals, dollar
 * amounts, the DR/MR/MRS/PHD abbreviations, single letters,
 * letter-digit part numbers) is preserved from the original.
 *
 * This module's output is the Wasser phoneme alphabet (ARPABET-flavored:
 * uppercase two-letter vowels, lowercase consonants); the translation to
 * SC-01 phone codes is arpabet_to_sc01.c's job, per NRL Report 7948's
 * own IPA-to-Votrax rules.
 */
#include <string.h>

#define MAX_LENGTH 128

/* ---- services the vendored 1985 files link against ------------------ */

static char *g_out;
static int g_max, g_len;

void outchar(int chr)
{
	if (g_len < g_max - 1)
		g_out[g_len++] = (char)chr;
}

void outstring(char *string)
{
	while (*string != '\0')
		outchar(*string++);
}

int makeupper(int character)
{
	return (character >= 'a' && character <= 'z')
		? character - 'a' + 'A' : character;
}

/* the vendored side (K&R definitions; int-returning by default) */
extern int xlate_word(char *word);
extern int say_cardinal(long value);
extern int say_ordinal(long value);
extern int say_ascii(int character);
extern int spell_word(char *word);

/* ---- input cursor: the original's 4-character look-ahead ------------ */

#define ENDC (-1)

static const char *g_in;
static int Char, Char1, Char2, Char3;

/* ctype without the UB on ENDC */
#define ISDIG(c) ((c) >= '0' && (c) <= '9')
#define ISALPH(c) (((c) >= 'A' && (c) <= 'Z') || ((c) >= 'a' && (c) <= 'z'))

static int nextraw(void)
{
	if (*g_in == '\0')
		return ENDC;
	return (unsigned char)*g_in++;
}

static int new_char(void)
{
	Char = Char1;
	Char1 = Char2;
	Char2 = Char3;
	if (Char3 != ENDC)
		Char3 = nextraw();
	return Char;
}

/* ---- word assembly, ported from the original parse.c ---------------- */

static void abbrev(char *buff)
{
	if (strcmp(buff, " DR ") == 0) {
		xlate_word(" DOCTOR ");
		new_char();
	} else if (strcmp(buff, " MR ") == 0) {
		xlate_word(" MISTER ");
		new_char();
	} else if (strcmp(buff, " MRS ") == 0) {
		xlate_word(" MISSUS ");
		new_char();
	} else if (strcmp(buff, " PHD ") == 0) {
		spell_word(" PHD ");
		new_char();
	} else
		xlate_word(buff);
}

static void have_letter(void)
{
	char buff[MAX_LENGTH];
	int count = 0;

	buff[count++] = ' ';   /* required initial blank */
	buff[count++] = (char)makeupper(Char);
	for (new_char(); ISALPH(Char) || Char == '\''; new_char()) {
		buff[count++] = (char)makeupper(Char);
		if (count > MAX_LENGTH - 2) {
			buff[count++] = ' ';
			buff[count] = '\0';
			xlate_word(buff);
			count = 1;
		}
	}
	buff[count++] = ' ';   /* required terminating blank */
	buff[count] = '\0';

	if (ISDIG(Char))
		spell_word(buff);            /* AAANNN part numbers */
	else if (strlen(buff) == 3)
		say_ascii(buff[1]);          /* single letter */
	else if (Char == '.')
		abbrev(buff);
	else {
		/* The exception dictionary (exceptions.c) supplies measured
		 * respellings for words the 1976 rules get wrong. */
		extern const char *ttv_exception(const char *spaced_word);
		const char *ex = ttv_exception(buff);
		if (ex) {
			char respelled[MAX_LENGTH];
			strcpy(respelled, ex);
			xlate_word(respelled);
		} else
			xlate_word(buff);
	}

	if (Char == '-' && ISALPH(Char1))
		new_char();                  /* skip hyphens inside words */
}

static void have_number(void)
{
	long value;
	int lastdigit;

	value = Char - '0';
	lastdigit = Char;
	for (new_char(); ISDIG(Char); new_char()) {
		value = 10 * value + (Char - '0');
		lastdigit = Char;
	}

	/* Recognize ordinals: 1ST, 22ND, 3RD, 4TH ... */
	switch (lastdigit) {
	case '1':
		if (makeupper(Char) == 'S' && makeupper(Char1) == 'T' &&
		    !ISALPH(Char2) && !ISDIG(Char2)) {
			say_ordinal(value);
			new_char();
			new_char();
			return;
		}
		break;
	case '2':
		if (makeupper(Char) == 'N' && makeupper(Char1) == 'D' &&
		    !ISALPH(Char2) && !ISDIG(Char2)) {
			say_ordinal(value);
			new_char();
			new_char();
			return;
		}
		break;
	case '3':
		if (makeupper(Char) == 'R' && makeupper(Char1) == 'D' &&
		    !ISALPH(Char2) && !ISDIG(Char2)) {
			say_ordinal(value);
			new_char();
			new_char();
			return;
		}
		break;
	default:
		if (makeupper(Char) == 'T' && makeupper(Char1) == 'H' &&
		    !ISALPH(Char2) && !ISDIG(Char2)) {
			say_ordinal(value);
			new_char();
			new_char();
			return;
		}
		break;
	}

	say_cardinal(value);

	if (Char == '.' && ISDIG(Char1)) {
		outstring("pOYnt ");
		for (new_char(); ISDIG(Char); new_char())
			say_ascii(Char);
	}

	while (ISALPH(Char)) {           /* trailing abbreviations: 5V, 33MHz */
		say_ascii(Char);
		new_char();
	}
}

static void have_dollars(void)
{
	long value = 0;

	for (new_char(); ISDIG(Char) || Char == ','; new_char())
		if (Char != ',')
			value = 10 * value + (Char - '0');

	say_cardinal(value);
	if (Char != '.' || !ISDIG(Char1)) {
		outstring(value == 1 ? "dAAlER " : "dAAlAArz ");
		return;
	}
	new_char();                      /* skip the period */
	if (ISDIG(Char1) && !ISDIG(Char2)) {
		outstring(value == 1 ? "dAAlER " : "dAAlAArz ");
		if (Char == '0' && Char1 == '0') {
			new_char();
			new_char();
			return;
		}
		outstring("AAnd ");
		value = (Char - '0') * 10 + Char1 - '0';
		say_cardinal(value);
		outstring(value == 1 ? "sEHnt " : "sEHnts ");
		new_char();
		new_char();
	}
}

/* Anything that is not a word, number or dollar amount.  Clause
 * punctuation becomes single marker characters in the phoneme stream;
 * arpabet_to_sc01.c turns them into the SC-01 pause phones per the NRL
 * report's own punctuation rules. */
static void have_special(void)
{
	int c = Char;

	if (c == ',' || c == ';' || c == ':')
		outchar(',');
	else if (c == '.' || c == '?' || c == '!')
		outchar('.');
	else if (c == '-')
		outchar('-');
	else
		outchar(' ');
	new_char();
}

/* ---- public entry points -------------------------------------------- */

int wasser_translate(const char *text, char *out, int out_max)
{
	g_in = text ? text : "";
	g_out = out;
	g_max = out_max;
	g_len = 0;
	Char = nextraw();
	Char1 = nextraw();
	Char2 = nextraw();
	Char3 = nextraw();
	while (Char != ENDC) {
		if (ISDIG(Char))
			have_number();
		else if (ISALPH(Char) || Char == '\'')
			have_letter();
		else if (Char == '$' && ISDIG(Char1))
			have_dollars();
		else
			have_special();
	}
	out[g_len] = '\0';
	return g_len;
}

/* Character-by-character spelling via the original's full ASCII name
 * table (say_ascii covers all 128 codes, punctuation names included). */
int wasser_spell(const char *text, char *out, int out_max)
{
	g_out = out;
	g_max = out_max;
	g_len = 0;
	for (; text && *text; text++) {
		say_ascii((unsigned char)*text & 0x7F);
		outchar(' ');
	}
	out[g_len] = '\0';
	return g_len;
}
