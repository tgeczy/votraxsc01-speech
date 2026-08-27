/* license:BSD-3-Clause
 * copyright-holders:tgeczy
 *
 * timestretch.h -- constant-pitch time scaling (SOLA) for the rate
 * setting.  The 1980 hardware had exactly one speed knob, the master
 * clock, and it chipmunks: faster is higher-pitched, smaller-headed
 * speech.  That behavior survives as the "authentic rate" option; this
 * module is the civilized default -- speed changes, pitch does not --
 * the same speed/clock split EchoTalk exposes and panthera's rate boost
 * implements.
 *
 * Streaming use (the NVDA driver): create once, vxs_feed() chip output,
 * vxs_pull() stretched audio, vxs_reset() on cancel.  One-shot use (the
 * SAPI engine): vxs_stretch_buffer() on a whole utterance.
 *
 * speed > 1 is faster (shorter output), < 1 slower.  Latency is about
 * one analysis frame (~26 ms at 40 kHz).
 */
#ifndef VX_TIMESTRETCH_H
#define VX_TIMESTRETCH_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(VX_BUILD_DLL)
#define VXS_API __declspec(dllexport)
#else
#define VXS_API
#endif

typedef struct vx_stretch vx_stretch;

VXS_API vx_stretch *vxs_create(void);
VXS_API void vxs_destroy(vx_stretch *s);
VXS_API void vxs_reset(vx_stretch *s);
VXS_API void vxs_set_speed(vx_stretch *s, double speed);   /* clamped 0.4..3 */
VXS_API void vxs_feed(vx_stretch *s, const int16_t *in, int count);
VXS_API int  vxs_pull(vx_stretch *s, int16_t *out, int max);

/* End of an utterance: emit the held sub-frame residual (so the ending
 * is heard) and clear the running state (so no crossfade tail bleeds
 * into the next utterance).  Pull afterwards to drain the residual. */
VXS_API void vxs_flush(vx_stretch *s);

/* One-shot: stretch a whole buffer, tail included.  Returns samples
 * written to out (at most max). */
VXS_API int vxs_stretch_buffer(const int16_t *in, int count, double speed,
                               int16_t *out, int max);

#ifdef __cplusplus
}
#endif

#endif /* VX_TIMESTRETCH_H */
