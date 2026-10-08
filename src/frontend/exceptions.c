/* license:BSD-3-Clause
 * copyright-holders:tgeczy
 *
 * exceptions.c -- the exception dictionary John A. Wasser invited in his
 * 1985 posting ("If you make a major addition (like better abbreviation
 * handling or an exception dictionary) please send me a copy").
 *
 * Every entry follows the same discipline: the word was MEASURED broken
 * through the NRL rules, a respelling was MEASURED correct, and a golden
 * test in tests/frontend_test.py pins the repaired pronunciation.  The
 * dictionary holds respellings, not phonemes, so entries inherit the full
 * rule machinery (plural voicing, suffixes, clause context) for free.
 *
 * The founding member is "search" -> "see-erch": a genuine gap in the
 * 1976 NRL rules for EA before R in mid-word position (word-initial
 * "earth"/"early" and "learn" are handled; "search"/"heard" are not).
 * The same lineage bug is audible in MacinTalk 1's reciter, which
 * descends from the same rules -- one 1976 gene, two descendants.
 */
#include <string.h>

typedef struct {
	const char *word;      /* as have_letter builds it: " UPPERCASE " */
	const char *respell;
} ttv_exception_entry;

static const ttv_exception_entry k_exceptions[] = {
	/* the EA-before-R gap: /er/ words */
	{ " SEARCH ",    " SURCH " },
	{ " SEARCHES ",  " SURCHES " },
	{ " SEARCHED ",  " SURCHED " },
	{ " SEARCHING ", " SURCHING " },
	{ " RESEARCH ",  " RESURCH " },
	{ " HEARD ",     " HURD " },
	{ " HEARSE ",    " HURSE " },
	{ " REHEARSE ",  " REHURSE " },
	{ " REHEARSAL ", " REHURSAL " },
	/* the EA-before-R gap: /eh/ words */
	{ " BEAR ",      " BAIR " },
	{ " BEARS ",     " BAIRS " },
	{ " WEAR ",      " WAIR " },
	{ " WEARS ",     " WAIRS " },
	{ " SWEAR ",     " SWAIR " },
	{ " SWEARS ",    " SWAIRS " },
	{ " PEAR ",      " PAIR " },
	{ " PEARS ",     " PAIRS " },
	/* soft C before E: the rules give "can-SELL" (S EH L) and, for
	 * -ed/-ing, "can-see-il" (S E I3 L).  Every screen-reader dialog has
	 * this word; respell onto the schwa (UH2) */
	{ " CANCEL ",     " CANSAL " },
	{ " CANCELS ",    " CANSALS " },
	{ " CANCELED ",   " CANSLED " },
	{ " CANCELLED ",  " CANSLED " },
	{ " CANCELING ",  " CANSLING " },
	{ " CANCELLING ", " CANSLING " },
};

const char *ttv_exception(const char *spaced_word)
{
	int i;
	for (i = 0; i < (int)(sizeof k_exceptions / sizeof k_exceptions[0]); i++)
		if (strcmp(k_exceptions[i].word, spaced_word) == 0)
			return k_exceptions[i].respell;
	return 0;
}
