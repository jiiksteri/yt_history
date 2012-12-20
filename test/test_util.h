#ifndef TEST_UTIL_H__INCLUDED

#include <stdarg.h>
#include <stdio.h>
#include <CUnit/CUnit.h>

#define DECLARE_TESTINFO(f) { #f, f, }

static inline void verbose(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	vprintf(fmt, ap);
	va_end(ap);
}

#endif
