# license:BSD-3-Clause
# copyright-holders:tgeczy
"""Golden tests for the text-to-phoneme frontend.

These run with NO ROM: the ttv_* functions never touch the chip, so the
whole NRL pipeline -- Wasser letter-to-sound plus the IPA-to-Votrax
mapping -- is testable on any machine that can build the DLL.  The build
script runs this file and fails the build on any mismatch, the same
parity-gate pattern panthera-speech uses for its abbreviation rules.

Golden values were transcribed from verified runs and each pins a
specific decision or repaired defect:

  hello        the lowercase-h fix (english.c never emits "HH")
  world/girl   the '[ER L]' editorial reading (the L must survive)
  gray         the repaired 'L [EY]' bracket-typo rule
  $1.50        the dollar path through the ported parse logic
  21st         ordinals; 1993 the era's "nineteen hundred ninety three"
  church       CH -> T CH per the NRL translation rules
  spell NVDA   spelling via spellword.c's full ASCII name table
"""

import ctypes
import os
import sys

DLL = os.environ.get("SC01_DLL", os.path.join(
	os.path.dirname(__file__), "..", "build", "x64", "sc01.dll"))

lib = ctypes.CDLL(os.path.abspath(DLL))
lib.vx_phone_name.restype = ctypes.c_char_p
lib.vx_phone_name.argtypes = [ctypes.c_ubyte]
for fn in (lib.ttv_translate, lib.ttv_spell):
	fn.restype = ctypes.c_int
	fn.argtypes = [ctypes.c_char_p, ctypes.c_char_p, ctypes.c_int]


def phones(text, spell=False):
	buf = ctypes.create_string_buffer(4096)
	fn = lib.ttv_spell if spell else lib.ttv_translate
	n = fn(text.encode(), buf, 4096)
	return " ".join(lib.vx_phone_name(b).decode() for b in buf.raw[:n])


CASES = [
	("hello world", False,
	 "H EH L UH3 O1 U1 PA0 W UH3 ER L D PA0"),
	("girl", False,
	 "G UH3 ER L PA0"),
	("lay", False,
	 "L UH3 A1 AY PA0"),
	("$1.50", False,
	 "W UH N PA0 D AH L UH3 ER PA0 AH N D PA0 F I F T E PA0 S EH N T S PA0"),
	("The 21st of May, 1993.", False,
	 "THV UH2 PA0 T W EH N T E PA0 F ER S T PA0 UH2 V PA0 M A AY PA1 PA0 "
	 "N AH E1 N T E N PA0 H UH N D R EH D PA0 N AH E1 N T E PA0 TH R E PA1 PA1"),
	("she washes the church watch", False,
	 "SH E PA0 W AH SH I Z PA0 THV UH2 PA0 T CH ER T CH PA0 W AH T T CH PA0"),
	("rubber baby buggy bumpers", False,
	 "R UH B B ER PA0 B A AY B E PA0 B UH G E PA0 B UH M P ER Z PA0"),
	("NVDA", True,
	 "EH N PA0 V E PA0 D E PA0 A AY PA0"),
]


def main():
	failures = 0
	for text, spell, want in CASES:
		got = phones(text, spell)
		if got != want:
			failures += 1
			print(f"FAIL {text!r} (spell={spell})")
			print(f"  want: {want}")
			print(f"  got:  {got}")
	# Degenerate inputs must not crash, and pure whitespace/punctuation
	# must not open with a pause (leading-pause suppression).
	for text in ("", " ", "..."):
		got = phones(text)
		if got != "":
			failures += 1
			print(f"FAIL degenerate {text!r} -> {got!r} (expected silence)")
	phones("'")   # spells "apostrophe"; only crash-freedom is asserted
	if failures:
		print(f"{failures} failure(s)")
		return 1
	print(f"frontend: {len(CASES)} golden cases and 4 degenerate inputs pass")
	return 0


if __name__ == "__main__":
	sys.exit(main())
