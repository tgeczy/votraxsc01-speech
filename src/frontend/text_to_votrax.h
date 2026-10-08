/* license:BSD-3-Clause
 * copyright-holders:tgeczy
 *
 * text_to_votrax.h -- English text to SC-01 phoneme codes.
 *
 * This is a pure translation module: text in, phoneme codes out.  It holds
 * no chip state and does no audio; the feeding loop belongs to whoever owns
 * the chip (the NVDA driver, the SAPI engine, or say01).  One C
 * implementation serves every consumer so pronunciation can never drift
 * between them.
 *
 * The translation lineage is the 1976 NRL letter-to-sound rules (Elovitz,
 * Johnson, McHugh & Shore, NRL Report 7948 -- a public-domain US
 * government work), the same rules the Votrax Type 'N Talk era grew up on.
 */
#ifndef VX_TEXT_TO_VOTRAX_H
#define VX_TEXT_TO_VOTRAX_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(VX_BUILD_DLL)
#define TTV_API __declspec(dllexport)
#else
#define TTV_API
#endif

/* Translate one run of text (UTF-8; non-ASCII is folded or skipped) into
 * SC-01 phoneme codes.  Returns the number of codes written to `phones`
 * (at most `max_phones`).  Translation is stateless across calls: feed
 * whole clauses for best prosody. */
TTV_API int ttv_translate(const char *text, uint8_t *phones, int max_phones);

/* Same, but spell character by character (for NVDA character echo and
 * spelling commands): letters become their letter names. */
TTV_API int ttv_spell(const char *text, uint8_t *phones, int max_phones);

/* The intermediate stage, for tools and tests: the rules' ARPAbet-style
 * stream (Wasser's phoneme names, space-separated words) for `text`.
 * Returns its length; `out` is always NUL-terminated. */
TTV_API int ttv_arpabet(const char *text, char *out, int out_max);

/* The pronouncing dictionary (lexicon.c, CMUdict): nonzero (the default)
 * pronounces every word it knows from the dictionary; zero sends all words
 * through the 1976 rules.  Process-wide, like the rest of the frontend. */
TTV_API void ttv_set_lexicon(int on);

#ifdef __cplusplus
}
#endif

#endif /* VX_TEXT_TO_VOTRAX_H */
