#ifndef VERBOSE_H__INCLUDED
#define VERBOSE_H__INCLUDED

enum verbosity_level {
	ERROR = -1,
	NORMAL,
	VERBOSE,
	FIREHOSE,
};

int verbose_adjust_level(int v);

void verbose(enum verbosity_level level, const char *fmt, ...);

#endif
