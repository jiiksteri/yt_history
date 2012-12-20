
#include "verbose.h"

#include <stdarg.h>
#include <stdio.h>

static enum verbosity_level verbosity_level;

int verbose_adjust_level(int v)
{
	return verbosity_level += v;
}

void verbose(enum verbosity_level level, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	if (verbosity_level >= level) {
		vprintf(fmt, ap);
	}
	va_end(ap);
}
