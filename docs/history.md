# The Votrax SC-01, briefly

Votrax began as the Vocal Interface Division of Federal Screw Works of
Troy, Michigan. Through the 1970s it built rack- and desktop-sized
phonetic synthesizers — Richard Gagnon's 1978 ICASSP paper describes the
architecture this whole family shares: a string of 8-bit phoneme commands,
**6 bits selecting the phoneme and 2 bits its inflection**, driving an
active filter network that models the vocal tract [3]. That is,
byte for byte, the interface `vx_write()` and `vx_inflection()` expose in
this repository, because the SC-01 put that same architecture on one chip.

The blind community met Votrax early: its synthesizers spoke inside
reading machines for the blind in the 1970s, well before personal
computers could talk [20]. When the SC-01 arrived (about 1980), the
same voice spread everywhere a phoneme bus could reach — the Type 'N Talk
serial appliance, the Apple II Mockingboard, the TRS-80 Voice Synthesizer,
Heathkit's HERO-1, and the arcade boards of Gorf, Wizard of Wor and
Q*bert. Interfacing it to microcomputers, text-to-phoneme conversion
included, was a published recipe by mid-decade [8], and researchers
drove it from phonetic transcriptions with prosodic markers, adapting the
allophone-adjustment hints straight out of the Votrax manual [5].

Two details of the era are preserved in this project's design:

- **Text-to-phoneme by rules.** The NRL letter-to-sound rules (Elovitz,
  Johnson, McHugh & Shore, NRL Report 7948, 1976 — a public-domain US
  government work) were the standard road from English text to phoneme
  codes in the SC-01's world, and they are the lineage of
  `src/frontend/`. Klatt's grand review of text-to-speech places this
  rule-program tradition in its wider history [2].
- **The Echo II connection.** In 1987, speech researchers formally
  compared VOTRAX and the Echo II — the TMS5220-based Apple II card — for
  intelligibility, in work aimed at communication aids [6]. Four
  decades later the two chips are neighbors again: the Echo II speaks
  through Jayson Smith's EchoTalk add-on, the SC-01 through this one, on
  the same screen reader.

The simulation this repository carries is MAME's: Olivier Galibert's
model of the decapped SC-01 die, filters built from the patent schematics,
the internal 512-byte phone ROM read out of the silicon itself. It is the
genuine article to the same degree the projects this one grew from —
BraiLab, PC-ROBOT, PC-TALKER, MacinTalk — insist on.

## References

- [2] [Review of text-to-speech conversion for English.](https://consensus.app/papers/details/23c1ce8b87da53e1b83735de151afc10/?utm_source=claude_desktop) (D. Klatt, 1987, The Journal of the Acoustical Society of America, DOI: 10.1121/1.395275)
- [3] [Votrax real time hardware for phoneme synthesis of speech](https://consensus.app/papers/details/125057c8297855068ae4757bb3ca88f1/?utm_source=claude_desktop) (R. Gagnon, 1978, ICASSP, DOI: 10.1109/icassp.1978.1170486)
- [5] [Driving the Votrax speech synthesizer from a wide phonetic transcription with high-level prosodic markers](https://consensus.app/papers/details/20f15907f7745fdf89bc4bb920ec9c90/?utm_source=claude_desktop) (I. Witten, 1982, International Journal of Man-Machine Studies, DOI: 10.1016/s0020-7373(82)80048-5)
- [6] [The intelligibility of synthesized speech: ECHO II versus VOTRAX.](https://consensus.app/papers/details/628a3e1032d1556aaecbe683a940de99/?utm_source=claude_desktop) (J. Hoover et al., 1987, Journal of Speech and Hearing Research, DOI: 10.1044/jshr.3003.425)
- [8] [Driving Votrax SCOI with microcomputers](https://consensus.app/papers/details/f04772bdd03350f5a9c9c4648f186335/?utm_source=claude_desktop) (O. R. Omotayo, 1985, Microprocessors and Microsystems, DOI: 10.1016/0141-9331(85)90054-7)
- [20] [News in Brief](https://consensus.app/papers/details/fe66da0bd12f545db91311ba507494fa/?utm_source=claude_desktop) (1977, Journal of Visual Impairment & Blindness, DOI: 10.1177/0145482x7707100721)
