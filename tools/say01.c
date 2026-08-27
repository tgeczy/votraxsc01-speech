/* license:BSD-3-Clause
 * copyright-holders:tgeczy
 *
 * say01 -- command-line probe for the SC-01 core.  This is the first place
 * the chip makes sound, and the reference harness everything else is
 * checked against.
 *
 *   say01 --rom roms/sc01a.bin --phones "H EH1 L OO1"        speak phonemes
 *   say01 --rom roms/sc01a.bin --table                       all 64 phones
 *   say01 --names                                            list the table
 *
 * Options: --variant sc01|sc01a   (default: guessed from ROM CRC)
 *          --clock <hz>           (default: 720000)
 *          --inflection <0..3>    (default: 0)
 *          --wav <file>           (default: say01.wav)
 */
#include "sc01.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- tiny WAV writer: 16-bit mono PCM ---- */

static void put32(FILE *f, unsigned v) { fputc(v, f); fputc(v >> 8, f); fputc(v >> 16, f); fputc(v >> 24, f); }
static void put16(FILE *f, unsigned v) { fputc(v, f); fputc(v >> 8, f); }

static int wav_write(const char *path, const int16_t *pcm, size_t n, unsigned rate)
{
	FILE *f = fopen(path, "wb");
	if (!f) { perror(path); return 0; }
	fwrite("RIFF", 1, 4, f); put32(f, 36 + (unsigned)(n * 2)); fwrite("WAVE", 1, 4, f);
	fwrite("fmt ", 1, 4, f); put32(f, 16); put16(f, 1); put16(f, 1);
	put32(f, rate); put32(f, rate * 2); put16(f, 2); put16(f, 16);
	fwrite("data", 1, 4, f); put32(f, (unsigned)(n * 2));
	fwrite(pcm, 2, n, f);
	fclose(f);
	return 1;
}

/* ---- growable sample buffer ---- */

typedef struct { int16_t *data; size_t len, cap; } pcmbuf;

static void pcm_append(pcmbuf *b, const int16_t *s, size_t n)
{
	if (b->len + n > b->cap) {
		b->cap = (b->cap ? b->cap * 2 : 65536);
		while (b->cap < b->len + n) b->cap *= 2;
		b->data = (int16_t *)realloc(b->data, b->cap * 2);
		if (!b->data) { fprintf(stderr, "out of memory\n"); exit(2); }
	}
	memcpy(b->data + b->len, s, n * 2);
	b->len += n;
}

/* Render until the chip raises ready again (phone finished), with a hard
 * cap so a misunderstanding can never hang the tool. */
static void render_until_ready(vx_chip *chip, pcmbuf *out, int cap_samples)
{
	int16_t block[256];
	int rendered = 0;
	while (!vx_ready(chip) && rendered < cap_samples) {
		int n = vx_render(chip, block, 256);
		pcm_append(out, block, (size_t)n);
		rendered += n;
	}
}

static void render_samples(vx_chip *chip, pcmbuf *out, int count)
{
	int16_t block[256];
	while (count > 0) {
		int n = vx_render(chip, block, count > 256 ? 256 : count);
		pcm_append(out, block, (size_t)n);
		count -= n;
	}
}

static unsigned char *read_file(const char *path, uint32_t *len_out)
{
	FILE *f = fopen(path, "rb");
	if (!f) { perror(path); return NULL; }
	fseek(f, 0, SEEK_END);
	long len = ftell(f);
	fseek(f, 0, SEEK_SET);
	unsigned char *buf = (unsigned char *)malloc(len > 0 ? (size_t)len : 1);
	if (fread(buf, 1, (size_t)len, f) != (size_t)len) { fclose(f); free(buf); return NULL; }
	fclose(f);
	*len_out = (uint32_t)len;
	return buf;
}

int main(int argc, char **argv)
{
	const char *rom_path = NULL, *wav_path = "say01.wav", *phones = NULL;
	const char *variant_name = NULL;
	unsigned clock_hz = VX_DEFAULT_CLOCK;
	int inflection = 0, table = 0, names = 0;

	for (int i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "--rom") && i + 1 < argc) rom_path = argv[++i];
		else if (!strcmp(argv[i], "--wav") && i + 1 < argc) wav_path = argv[++i];
		else if (!strcmp(argv[i], "--phones") && i + 1 < argc) phones = argv[++i];
		else if (!strcmp(argv[i], "--variant") && i + 1 < argc) variant_name = argv[++i];
		else if (!strcmp(argv[i], "--clock") && i + 1 < argc) clock_hz = (unsigned)atoi(argv[++i]);
		else if (!strcmp(argv[i], "--inflection") && i + 1 < argc) inflection = atoi(argv[++i]);
		else if (!strcmp(argv[i], "--table")) table = 1;
		else if (!strcmp(argv[i], "--names")) names = 1;
		else { fprintf(stderr, "unknown option: %s\n", argv[i]); return 2; }
	}

	if (names) {
		for (int i = 0; i < 64; i++)
			printf("%02x %s\n", i, vx_phone_name((uint8_t)i));
		return 0;
	}
	if (!rom_path) {
		fprintf(stderr,
		        "say01: --rom <file> is required (the 512-byte SC-01 internal ROM).\n"
		        "Expected files: sc01.bin (CRC 528d1c57) or sc01a.bin (CRC fc416227).\n");
		return 2;
	}

	uint32_t rom_len = 0;
	unsigned char *rom = read_file(rom_path, &rom_len);
	if (!rom)
		return 2;

	int variant;
	if (variant_name)
		variant = strcmp(variant_name, "sc01a") ? VX_SC01 : VX_SC01A;
	else {
		/* Guess the variant from the file's CRC so the common case needs
		 * no flag; vx_create re-verifies. */
		uint32_t crc = vx_crc32(rom, rom_len);
		variant = (crc == vx_expected_crc(VX_SC01)) ? VX_SC01 : VX_SC01A;
	}

	char error[256];
	vx_chip *chip = vx_create(variant, clock_hz, rom, rom_len, error, sizeof error);
	free(rom);
	if (!chip) {
		fprintf(stderr, "say01: %s\n", error);
		return 1;
	}

	unsigned rate = (unsigned)(vx_sample_rate(chip) + 0.5);
	printf("%s at %u Hz clock -> %u Hz audio\n",
	       variant == VX_SC01A ? "SC-01A" : "SC-01", clock_hz, rate);
	vx_inflection(chip, (uint8_t)inflection);

	pcmbuf out = { 0 };
	int cap = (int)(rate * 4);   /* no phone lasts four seconds */

	if (table) {
		/* Speak every phone in ROM order, a short pause between them, and
		 * print where each one starts so a listener can index the wav. */
		for (int p = 0; p < 64; p++) {
			printf("%8.3fs  %02x %s\n", out.len / (double)rate, p, vx_phone_name((uint8_t)p));
			vx_write(chip, (uint8_t)p);
			render_until_ready(chip, &out, cap);
			vx_write(chip, VX_PHONE_PA1);      /* breathing room */
			render_until_ready(chip, &out, cap);
		}
		vx_write(chip, VX_PHONE_STOP);
		render_samples(chip, &out, (int)(rate / 4));
	} else if (phones) {
		char *spec = _strdup(phones);
		for (char *tok = strtok(spec, " ,\t"); tok; tok = strtok(NULL, " ,\t")) {
			int p = vx_phone_by_name(tok);
			if (p < 0) {
				fprintf(stderr, "say01: unknown phoneme '%s' (--names lists them)\n", tok);
				return 2;
			}
			vx_write(chip, (uint8_t)p);
			render_until_ready(chip, &out, cap);
		}
		free(spec);
		vx_write(chip, VX_PHONE_STOP);
		render_samples(chip, &out, (int)(rate / 4));
	} else {
		fprintf(stderr, "say01: nothing to do; use --phones, --table or --names\n");
		return 2;
	}

	vx_destroy(chip);
	if (!wav_write(wav_path, out.data, out.len, rate))
		return 1;
	printf("%zu samples (%.2f s) -> %s\n", out.len, out.len / (double)rate, wav_path);
	free(out.data);
	return 0;
}
