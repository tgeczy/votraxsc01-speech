/* license:BSD-3-Clause
 * copyright-holders:tgeczy
 *
 * sc01.h -- flat C API over the MAME Votrax SC-01/SC-01A simulation.
 *
 * This is the one interface every consumer shares: the say01 probe tool,
 * the NVDA add-on (via ctypes) and the SAPI engine all talk to the chip
 * through these functions and nothing else.
 *
 * Model of use:
 *   1. vx_create() with a 512-byte internal ROM dump (CRC-verified).
 *   2. Poll vx_ready(); when it returns nonzero, vx_write() the next
 *      phoneme (0..63).  Inflection may change any time via
 *      vx_inflection().
 *   3. vx_render() pulls signed 16-bit mono samples at vx_sample_rate()
 *      (master clock / 18; 40 kHz at the standard 720 kHz clock).
 *      Rendering is what advances emulated time -- a chip that is not
 *      rendered never becomes ready.
 *
 * Threading: one vx_chip may be used by one thread at a time.
 */
#ifndef VX_SC01_H
#define VX_SC01_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(VX_BUILD_DLL)
#define VX_API __declspec(dllexport)
#else
#define VX_API
#endif

typedef struct vx_chip vx_chip;

enum vx_variant {
	VX_SC01  = 0,   /* earlier mask ROM  (sc01.bin,  CRC 528d1c57) */
	VX_SC01A = 1    /* later mask ROM    (sc01a.bin, CRC fc416227) */
};

#define VX_ROM_SIZE       512
#define VX_DEFAULT_CLOCK  720000u   /* datasheet-standard master clock */
#define VX_PHONE_STOP     0x3f
#define VX_PHONE_PA1      0x3e      /* the long pause phone */

/* Create a chip.  `rom` must be VX_ROM_SIZE bytes and match the variant's
 * mask-ROM CRC32; on failure returns NULL and writes a human-readable
 * reason (including the expected and measured CRCs) into `error`. */
VX_API vx_chip *vx_create(int variant, uint32_t clock_hz,
                          const uint8_t *rom, uint32_t rom_len,
                          char *error, size_t error_len);

VX_API void vx_destroy(vx_chip *chip);

/* Return to the power-on state (phone register = STOP, ready asserted). */
VX_API void vx_reset(vx_chip *chip);

/* Change the master clock.  Sample rate follows (clock / 18); pending
 * phone timing is rescaled exactly as the hardware would. */
VX_API void     vx_set_clock(vx_chip *chip, uint32_t clock_hz);
VX_API uint32_t vx_clock(const vx_chip *chip);
VX_API double   vx_sample_rate(const vx_chip *chip);

/* Latch a phoneme (low 6 bits used).  Equivalent to the hardware strobe. */
VX_API void vx_write(vx_chip *chip, uint8_t phone);

/* Set the 2-bit inflection input. */
VX_API void vx_inflection(vx_chip *chip, uint8_t level);

/* Closure timing (src/chip/votrax.cpp, docs/closure-study.md).  Nonzero
 * (the default) latches the closure on the patent's shared noise/closure
 * delay, restoring the P/T/K release bursts; zero is upstream MAME's
 * behaviour, bit for bit. */
VX_API void vx_closure_fix(vx_chip *chip, int on);

/* The A/R (acknowledge/request) line: nonzero when the chip wants the
 * next phoneme. */
VX_API int vx_ready(vx_chip *chip);

/* Render up to `count` samples of signed 16-bit mono audio.  Returns the
 * number of samples written (always `count`; the chip has no underrun --
 * silence and sustained phones are still samples). */
VX_API int vx_render(vx_chip *chip, int16_t *out, int count);

/* ROM bookkeeping, for loaders and error messages. */
VX_API uint32_t vx_expected_crc(int variant);
VX_API uint32_t vx_crc32(const uint8_t *data, size_t len);

/* Phoneme table helpers ("EH3".."STOP", case-insensitive lookup). */
VX_API const char *vx_phone_name(uint8_t phone);
VX_API int vx_phone_by_name(const char *name);

#ifdef __cplusplus
}
#endif

#endif /* VX_SC01_H */
