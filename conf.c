#include "conf.h"

#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>

static char *full_name(const char *tail, char *buf, int sz)
{
	/* Might want to check for overruns, though it will fail
	 * at fopen() later.
	 */
	snprintf(buf, sz, "%s/.yt_history/%s", getenv("HOME"), tail);

	return buf;
}

int conf_read(const char *fname, char *buf, int sz)
{
	char fnbuf[512];

	FILE *f = fopen(full_name(fname, fnbuf, sizeof(fnbuf)), "r");
	if (f == NULL) {
		return errno;
	}

	fgets(buf, sz, f);
	buf[strlen(buf)-1] = '\0';
	fclose(f);

	return 0;

}
