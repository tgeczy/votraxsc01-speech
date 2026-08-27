// license:BSD-3-Clause
// Shim replacing MAME's logmacro.h: logging is compiled out entirely.
// The arguments of LOGMASKED are never evaluated, which is what lets the
// one machine().time() call in votrax.cpp vanish without a machine object.
#pragma once

#define LOGMASKED(...) do {} while (0)
#define LOG(...) do {} while (0)
