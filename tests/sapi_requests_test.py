# license:BSD-3-Clause
# copyright-holders:tgeczy
"""What SAPI can ask of the engine beyond plain text, on the test build of
the SAPI DLL through tests/sapi_harness.cpp: how the site takes writes, the
program's rate and volume, a fragment's rate, volume and pitch, a <spell>,
a skip request, and when each bookmark's event arrives.

The engine is held to itself -- a request spoken one way must come out as
the same request spoken the other way -- so nothing is compared with a
recording.  Needs a ROM (roms/sc01a.bin, else roms/sc01.bin) beside the
test DLL; without one it says SKIPPED, which is not a pass.

The checks:
  - writes: a site that takes at most 1000 bytes of a Write (or 999, an odd
    count), and one that refuses each block twice (a Write that takes
    nothing: Windows 7's full buffer), get the same audio as a site that
    takes everything, the 50 ms of silence at the end included;
  - rate: a fragment's RateAdj (+4), the program's rate (+4), and +2 with
    +2, are the same audio, and shorter than rate 0's;
  - volume: the program's 50 halves the samples; a fragment's 50 is the
    same audio; 50 of 50 is 25;
  - a change inside a request reaches that fragment only: a pitch change
    leaves the words before it as they are and does not stop the chip (no
    STOP phone and ring-out at the change, as the NVDA driver has none); a
    pitch change within one of the chip's four levels is no change; a
    faster fragment shortens the request; a quieter one differs in its own
    stretch only;
  - JAWS's request (a word a fragment, a bookmark between every pair) is
    the joined text's audio, each bookmark sent as its audio is written;
  - spell: a <spell> fragment is not the word, and inside a sentence it is
    the same audio as three requests;
  - skip: +1, 0, +2 and -1 are answered with that count and speech goes on
    from that sentence; more sentences than there are, a move back from
    the first, and a kind that is not sentences are answered with what
    could be done and the request ends; an abort with a skip is an abort;
  - bookmarks: one before any text arrives before any audio; one between
    two fragments of different pitch arrives between them; one at the end
    arrives after the audio and before the silence;
  - the two masks are two voices, and "authentic rate" is another rate
    model (the harness reaches both).

VOTRAX_SAPI_REQUESTS_BREAK=write-once | markup | spell | skip | marks-end
puts one fault into every request (the test DLL's VOTRAX_SAPI_TEST_BREAK;
the shipped DLL has no such switch).  --controls runs each and passes only
if each one fails the checks it should: a check whose control passes is
not testing anything.

    python tests/sapi_requests_test.py [--arch x64|x86|both] [--verbose] [--controls]
"""

import array
import concurrent.futures
import os
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
STAGE = os.environ.get("VOTRAX_SAPI_STAGE", os.path.join(HERE, "..", "build", "sapitest"))
ARGS = sys.argv[1:]
ARCHES = ["x64", "x86"]
if "--arch" in ARGS and ARGS[ARGS.index("--arch") + 1] != "both":
	ARCHES = [ARGS[ARGS.index("--arch") + 1]]
BREAK = os.environ.get("VOTRAX_SAPI_REQUESTS_BREAK", "-") or "-"

RATE = 22050                              # the engine's fixed output rate
PAD = b"\0" * (RATE // 20 * 2)            # its 50 ms of silence after a request
CHUNK = 4096                              # bytes it offers SAPI at a time
# The ring-out after the STOP phone that closes an utterance: 20 blocks of
# 512 samples at the chip's 40000 Hz, in output bytes.
TAIL = int(20 * 512 * RATE / 40000) * 2
NEAR = 16                                 # bytes: resampling's rounding at a join

TEXT = "Hello, how are you today? I am fine."
THREE = ["One is first.", "Two is second.", "Three is third."]
PARTS = ["Hello there,", "how are you", "today."]
#: writes taken before the move back is asked: in the second sentence (checked)
BACK_AT = 28

#: control -> a word every check it must fail carries in its label
CONTROLS = {
	"write-once": "writes:",
	"markup": "markup:",
	"spell": "spell:",
	"skip": "skip",
	"marks-end": "bookmark",
}


def S(text, rate=0, volume=100, middle=0):
	return "S:%d:%d:%d:%s" % (rate, volume, middle, text)


def L(text):
	return "L:0:100:0:" + text


def B(name):
	return "B:%s" % name


def req(frags, rate=0, volume=100, writes="all", actions="-"):
	return "\t".join([str(rate), str(volume), BREAK, writes, actions] + list(frags))


# name -> the requests one engine speaks in order.  Every process is a new
# engine and so a new chip; requests that are compared start alike.  A name
# ending in "@auth" runs under authentic rate, one in "@sc01" on the other
# mask.
PROCS = {
	"plain": [req([S(TEXT)])],
	"max": [req([S(TEXT)], writes="max=1000")],
	"odd": [req([S(TEXT)], writes="max=999")],
	"zero": [req([S(TEXT)], writes="zero=2")],
	"rate-site": [req([S(TEXT)], rate=4)],
	"rate-frag": [req([S(TEXT, rate=4)])],
	"rate-both": [req([S(TEXT, rate=2)], rate=2)],
	"vol-site": [req([S(TEXT)], volume=50)],
	"vol-frag": [req([S(TEXT, volume=50)])],
	"vol-both": [req([S(TEXT, volume=50)], volume=50)],
	"vol-25": [req([S(TEXT)], volume=25)],
	"pitch-mid": [req([S(PARTS[0]), S(PARTS[1], middle=8), S(PARTS[2])])],
	"pitch-sep": [req([S(PARTS[0])]), req([S(PARTS[1], middle=8)]), req([S(PARTS[2])])],
	"pitch-flat": [req([S(PARTS[0]), S(PARTS[1]), S(PARTS[2])])],
	"pitch-near": [req([S(PARTS[0]), S(PARTS[1], middle=2), S(PARTS[2])])],
	"rate-mid": [req([S(PARTS[0]), S(PARTS[1], rate=6), S(PARTS[2])])],
	"vol-mid-40": [req([S(PARTS[0]), S(PARTS[1], volume=40), S(PARTS[2])])],
	"vol-mid-20": [req([S(PARTS[0]), S(PARTS[1], volume=20), S(PARTS[2])])],
	"jaws": [req([S("Hello,"), B(1), S("how"), B(2), S("are"), B(3), S("you"), B(4), S("today?"), B(5)])],
	"jaws-joined": [req([S("Hello, how are you today?")])],
	"spell": [req([L("cat")])],
	"spell-word": [req([S("cat")])],
	"spell-mid": [req([S("The word is"), L("cat"), S("as in cat.")])],
	"spell-sep": [req([S("The word is")]), req([L("cat")]), req([S("as in cat.")])],
	"three": [req([S(" ".join(THREE))])],
	"from-two": [req([S(" ".join(THREE[1:]))])],
	"from-three": [req([S(THREE[2])])],
	"skip+1": [req([S(" ".join(THREE))], actions="skip@3:1")],
	"skip-1": [req([S(" ".join(THREE))], actions="skip@%d:-1" % BACK_AT)],
	"skip0": [req([S(" ".join(THREE))], actions="skip@3:0")],
	"skip+2": [req([S(" ".join(THREE))], actions="skip@3:2")],
	"skip+5": [req([S(" ".join(THREE))], actions="skip@3:5")],
	"skip-1-first": [req([S(" ".join(THREE))], actions="skip@3:-1")],
	"skip-kind": [req([S(" ".join(THREE))], actions="skip@3:1:x")],
	"skip-abort": [req([S(" ".join(THREE))], actions="abort@3,skip@3:1")],
	"mark-start": [req([B(1), S(TEXT)])],
	"mark-seam": [req([S(PARTS[0]), B(2), S(PARTS[1], middle=8)])],
	"mark-end": [req([S(TEXT), B(3)])],
	"plain@sc01": [req([S(TEXT)])],
	"plain@auth": [req([S(TEXT)])],
	"rate-site@auth": [req([S(TEXT)], rate=4)],
	"rate-frag@auth": [req([S(TEXT, rate=4)])],
}


def roms(arch):
	return [v for v in ("sc01a", "sc01") if os.path.isfile(os.path.join(STAGE, arch, v + ".bin"))]


def harness(arch, name, lines, tmp, variant):
	"""One harness process speaking lines in order: a list of results."""
	job = os.path.join(tmp, "job_%s_%s.txt" % (arch, name))
	out = os.path.join(tmp, "%s_%s" % (arch, name))
	os.makedirs(out, exist_ok=True)
	with open(job, "w", encoding="utf-8", newline="\n") as f:
		f.write("\n".join(lines) + "\n")
	r = subprocess.run([os.path.join(STAGE, arch, "sapi_harness.exe"),
		os.path.join(STAGE, arch, "votrax_sapi.dll"), job, out, variant,
		"1" if name.endswith("@auth") else "0"],
		capture_output=True, text=True, timeout=300)
	got = []
	for ln in r.stdout.splitlines():
		if not ln.startswith("case "):
			continue
		f = dict(x.split("=", 1) for x in ln.split()[2:] if "=" in x)
		k = int(ln.split()[1])
		events = [] if f["events"] == "-" else [
			(e.split("@")[0], int(e.split("@")[1].split("/")[0]), int(e.split("/")[1]))
			for e in f["events"].split(",")]
		with open(os.path.join(out, "case_%d.pcm" % k), "rb") as p:
			pcm = p.read()
		got.append(dict(hr=int(f["hr"], 16), writes=int(f["writes"]), calls=int(f["calls"]),
			events=events, skips=[] if f["skips"] == "-" else [int(x) for x in f["skips"].split(",")],
			pcm=pcm))
	if len(got) != len(lines):
		sys.exit("FAILED: the %s harness ran %d of %d requests of %s: %s" % (
			arch, len(got), len(lines), name, (r.stdout + r.stderr)[-800:]))
	return got


def rms(pcm):
	a = array.array("h", pcm[:len(pcm) - len(pcm) % 2])
	return (sum(x * x for x in a[::3]) / max(1, len(a[::3]))) ** 0.5


def body(r):
	"""A request's audio without the engine's silence at its end."""
	return r["pcm"][:-len(PAD)] if r["pcm"].endswith(PAD) else r["pcm"]


def secs(n):
	return "%.2f s" % (n / 2 / RATE)


def head(a, b):
	"""How many bytes a and b begin alike."""
	n, m = 0, min(len(a), len(b))
	while n < m and a[n] == b[n]:
		n += 1
	return n - n % 2


def checks(g, both_masks):
	"""[(label, ok, what)] for one arch's results g: {process: [result, ...]}."""
	out = []

	def add(label, ok, what=""):
		out.append((label, bool(ok), what))

	whole = lambda name: g[name][0]["pcm"]                  # noqa: E731
	a = whole("plain")
	add("a plain request is spoken", g["plain"][0]["hr"] == 0 and rms(a) > 300 and a.endswith(PAD), secs(len(a)))

	# the writes
	for name, how in (("max", "at most 1000 bytes of each Write taken"), ("odd", "an odd count, 999, taken"),
			("zero", "each block refused twice first")):
		r = g[name][0]
		add("writes: %s" % how, r["hr"] == 0 and r["pcm"] == a,
			"the same audio, %d Write calls" % r["calls"] if r["pcm"] == a else
			"%s of %s arrived, alike for %s" % (secs(len(r["pcm"])), secs(len(a)), secs(head(r["pcm"], a))))

	# the program's rate and volume with a fragment's
	fast = whole("rate-site")
	add("rate: the program's +4 is faster than 0", len(fast) < 0.9 * len(a), "%s against %s" % (secs(len(fast)), secs(len(a))))
	add("markup: a fragment's +4 is the program's +4", whole("rate-frag") == fast, secs(len(whole("rate-frag"))))
	add("markup: +2 on the program's +2 is +4", whole("rate-both") == fast, secs(len(whole("rate-both"))))
	half, ratio = whole("vol-site"), rms(whole("vol-site")) / max(1.0, rms(a))
	add("volume: the program's 50 halves the samples", len(half) == len(a) and 0.42 < ratio < 0.58, "%.2f of full" % ratio)
	add("volume: a fragment's 50 is the program's 50", whole("vol-frag") == half)
	add("markup: 50 of the program's 50 is 25", whole("vol-both") == whole("vol-25"),
		"%.2f of full" % (rms(whole("vol-both")) / max(1.0, rms(a))))

	# a change inside a request
	mid, flat = body(g["pitch-mid"][0]), body(g["pitch-flat"][0])
	sep = [body(x) for x in g["pitch-sep"]]
	alike = head(mid, sep[0])
	add("markup: a pitch change leaves the words before it as they were",
		mid != flat and alike >= len(sep[0]) - TAIL - NEAR,
		"alike for %s of the first fragment's %s" % (secs(alike), secs(len(sep[0]) - TAIL)))
	want = sum(len(x) for x in sep) - 2 * TAIL
	add("markup: the chip is not stopped at a pitch change", abs(len(mid) - want) <= 2 * NEAR,
		"%s; three requests less two ring-outs are %s" % (secs(len(mid)), secs(want)))
	add("a pitch change within one of the chip's four levels is no change", whole("pitch-near") == whole("pitch-flat"))
	quick = body(g["rate-mid"][0])
	add("markup: a faster fragment shortens the request", len(quick) < len(mid) - RATE // 10,
		"%s against %s" % (secs(len(quick)), secs(len(mid))))
	v40, v20 = body(g["vol-mid-40"][0]), body(g["vol-mid-20"][0])
	first = head(v40, v20)
	last = len(v40) - head(v40[::-1], v20[::-1])
	add("markup: a quieter fragment differs in its own stretch only",
		len(v40) == len(v20) and len(sep[0]) - TAIL - NEAR <= first < last <= len(v40) - RATE // 5,
		"they differ from %s to %s of %s" % (secs(first), secs(last), secs(len(v40))))

	# JAWS
	r = g["jaws"][0]
	ev = r["events"]
	add("JAWS: a word a fragment is the joined text", r["pcm"] == whole("jaws-joined"))
	add("JAWS: its five bookmarks, in order", [e[0] for e in ev] == ["1", "2", "3", "4", "5"],
		",".join(e[0] for e in ev) or "none")
	add("bookmarks inside an utterance are sent as its audio is written",
		len(ev) == 5 and all(w == o for _n, w, o in ev) and all(x[1] < y[1] for x, y in zip(ev, ev[1:]))
		and ev[0][1] < len(body(r)) // 2,
		" ".join("%s at %s" % (n, secs(w)) for n, w, _o in ev) + " of %s" % secs(len(body(r))))

	# spell
	add("spell: not the word", whole("spell") != whole("spell-word") and rms(whole("spell")) > 300,
		"%s spelled, %s as a word" % (secs(len(whole("spell"))), secs(len(whole("spell-word")))))
	sep = b"".join(body(x) for x in g["spell-sep"]) + PAD
	add("spell: inside a sentence, the same as three requests", whole("spell-mid") == sep,
		"%s against %s" % (secs(len(whole("spell-mid"))), secs(len(sep))))

	# skip
	three = whole("three")
	two_at = len(body(g["three"][0])) - len(body(g["from-two"][0]))
	three_at = len(body(g["three"][0])) - len(body(g["from-three"][0]))
	add("the move back is asked in the second sentence", two_at + CHUNK < BACK_AT * CHUNK < three_at - CHUNK,
		"asked at %s; the second sentence runs %s to %s" % (secs(BACK_AT * CHUNK), secs(two_at), secs(three_at)))
	for name, count, rest in (("skip+1", 1, "from-two"), ("skip0", 0, "three"), ("skip+2", 2, "from-three"),
			("skip-1", -1, "three")):
		r = g[name][0]
		taken = head(r["pcm"], three)
		after = len(body(r)) - taken
		want = len(body(g[rest][0]))
		add("skip %+d: answered %d, and speech goes on from there" % (count, count),
			r["hr"] == 0 and r["skips"] == [count] and r["pcm"].endswith(PAD) and abs(after - want) <= 0.02 * want
			and rms(r["pcm"][taken:]) > 300,
			"answered %s; %s after the skip, %s alone" % (r["skips"], secs(after), secs(want)))
	for name, answer, what in (("skip+5", 2, "5 asked with 2 sentences left"), ("skip-1-first", 0, "back from the first"),
			("skip-kind", 0, "a kind that is not sentences")):
		r = g[name][0]
		add("skip (%s): answered %d and the request ends" % (what, answer),
			r["hr"] == 0 and r["skips"] == [answer] and r["writes"] == 3 and len(r["pcm"]) == 3 * CHUNK,
			"answered %s, %d writes" % (r["skips"], r["writes"]))
	r = g["skip-abort"][0]
	add("an abort with a skip is an abort", r["hr"] == 0 and r["skips"] == [] and r["writes"] == 3,
		"answered %s, %d writes" % (r["skips"], r["writes"]))

	# bookmarks
	r = g["mark-start"][0]
	add("bookmark before the text: before any audio", r["events"] == [("1", 0, 0)] and r["pcm"] == a, str(r["events"]))
	r = g["mark-seam"][0]
	ev = r["events"]
	add("bookmark between two fragments of different pitch: between them",
		len(ev) == 1 and ev[0][0] == "2" and ev[0][1] == ev[0][2]
		and abs(ev[0][1] - (len(body(g["pitch-sep"][0])) - TAIL)) <= NEAR, str(ev))
	r = g["mark-end"][0]
	add("bookmark at the end: after the audio, before the silence",
		r["events"] == [("3", len(a) - len(PAD), len(a) - len(PAD))] and r["pcm"] == a, str(r["events"]))

	# the harness reaches the mask and the rate model
	if both_masks:
		add("the two masks are two voices", whole("plain@sc01") != a and rms(whole("plain@sc01")) > 300)
	auth = whole("plain@auth")
	add("authentic rate: spoken, and at rate 0 the same audio", auth == a)
	add("authentic rate: +4 is another +4, faster than 0, a fragment's the program's",
		whole("rate-site@auth") != fast and len(whole("rate-site@auth")) < 0.9 * len(a)
		and whole("rate-frag@auth") == whole("rate-site@auth"),
		"%s against %s by truncation" % (secs(len(whole("rate-site@auth"))), secs(len(fast))))
	return out


def run_checks():
	"""Every check on every arch: (failed labels, lines to print), or None when skipped."""
	for arch in ARCHES:
		for f in ("votrax_sapi.dll", "sapi_harness.exe"):
			if not os.path.isfile(os.path.join(STAGE, arch, f)):
				sys.exit("FAILED: %s is missing: .\\build.ps1 -Target sapitest" % os.path.join(STAGE, arch, f))
		if not roms(arch):
			return None
	tmp = tempfile.mkdtemp(prefix="votrax_sapi_requests_")
	failed, lines = [], []
	try:
		with concurrent.futures.ThreadPoolExecutor(max_workers=8) as pool:
			fut = {}
			for arch in ARCHES:
				have = roms(arch)
				for name, reqs in PROCS.items():
					if name.endswith("@sc01") and len(have) < 2:
						continue
					fut[arch, name] = pool.submit(harness, arch, name, reqs, tmp,
						"sc01" if name.endswith("@sc01") else have[0])
			for arch in ARCHES:
				res = checks({n: f.result() for (a, n), f in fut.items() if a == arch}, len(roms(arch)) == 2)
				for label, ok, what in res:
					if not ok:
						failed.append(label)
					if not ok or "--verbose" in ARGS:
						lines.append("%-4s %s %s%s" % ("ok" if ok else "FAIL", arch, label, ": " + what if what else ""))
				lines.append("sapi requests %s (%s): %d of %d right" % (
					arch, roms(arch)[0], sum(ok for _l, ok, _w in res), len(res)))
	finally:
		shutil.rmtree(tmp, ignore_errors=True)
	return failed, lines


def controls():
	"""Each fault put back must fail the checks that guard against it."""
	bad = 0
	for name, word in CONTROLS.items():
		r = subprocess.run([sys.executable, os.path.abspath(__file__)] + [x for x in ARGS if x != "--controls"],
			capture_output=True, text=True, env=dict(os.environ, VOTRAX_SAPI_REQUESTS_BREAK=name))
		if "SKIPPED" in r.stdout:
			print(r.stdout.strip())
			return 0
		fails = [ln for ln in r.stdout.splitlines() if ln.startswith("FAIL")]
		seen = [ln for ln in fails if word in ln]
		ok = r.returncode == 1 and seen
		bad += not ok
		print("%-4s control %-10s %s" % ("ok" if ok else "FAIL", name,
			"fails %d checks, %d of them its own" % (len(fails), len(seen)) if ok else
			"did not fail a check with '%s' in it: the check is not testing it" % word))
	print("sapi requests controls: %s" % ("ok" if not bad else "%d FAILED" % bad))
	return 1 if bad else 0


def main():
	if "--controls" in ARGS:
		sys.exit(controls())
	got = run_checks()
	if got is None:
		print("sapi requests: SKIPPED, no ROM beside the test DLL (roms/sc01a.bin or roms/sc01.bin)")
		sys.exit(0)
	failed, lines = got
	print("\n".join(lines))
	print("sapi requests: %s" % ("ok" if not failed else "%d FAILED" % len(failed)))
	sys.exit(1 if failed else 0)


if __name__ == "__main__":
	main()
