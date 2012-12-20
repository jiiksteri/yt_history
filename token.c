#include "token.h"

#include <stdlib.h>
#include <errno.h>
#include <string.h>

#include <event2/buffer.h>

#include <json.h>

#include "verbose.h"

static int copy_value_to_token_int(int *target, struct json_object *obj, const char *key)
{
	struct json_object *value;
	int ret;

	if (json_object_object_get_ex(obj, key, &value)) {
		*target = json_object_get_int(value);
		verbose(VERBOSE, "%s(): %s -> %d\n", __func__, key, *target);
		ret = 0;
	} else {
		ret = EINVAL;
	}
	return ret;
}


static int copy_value_to_token_string(char **target, struct json_object *obj, const char *key)
{
	struct json_object *value;
	int ret;

	if (json_object_object_get_ex(obj, key, &value)) {
		*target = strdup(json_object_get_string(value));
		verbose(VERBOSE, "%s(): %s -> %s\n", __func__, key, *target);
		ret = 0;
	} else {
		ret = EINVAL;
	}

	return ret;
}

static int build_token_into(struct access_token *token, struct json_object *obj)
{
	int err;

	verbose(FIREHOSE, "%s(): Building token from %p (type '%s')\n",
		__func__, obj, json_type_to_name(json_object_get_type(obj)));

	err = copy_value_to_token_string(&token->access_token, obj, "access_token");
	if (err != 0) {
		verbose(ERROR, "%s(): No 'access_token' member in token json (%s)\n",
			__func__, strerror(err));
		return err;
	}

	err = copy_value_to_token_int(&token->expires_in, obj, "expires_in");
	if (err != 0) {
		verbose(ERROR, "%s(): No 'expires_in' member in token json (%s)\n",
			__func__, strerror(err));
	}

	return err;
}

int token_parse_json(struct access_token **tokenp, struct evbuffer *buf)
{
	char cbuf[1024];
	int removed;
	int ret;

	struct access_token *token;

	struct json_tokener *tokener;
	enum json_tokener_error jerr;
	struct json_object *obj;

	tokener = json_tokener_new();
	if (tokener == NULL) {
		return ENOMEM;
	}

	do {
		removed = evbuffer_remove(buf, cbuf, sizeof(cbuf));
		obj = json_tokener_parse_ex(tokener, cbuf, removed);
		jerr = json_tokener_get_error(tokener);
		verbose(FIREHOSE, "%s(): Passed %d bytes, result %p (%s), remaining %zd\n",
			__func__, removed, obj, json_tokener_error_desc(jerr),
		       evbuffer_get_length(buf));
	} while (obj == NULL && jerr == json_tokener_continue && evbuffer_get_length(buf) > 0);

	json_tokener_free(tokener);

	if (obj != NULL) {
		token = malloc(sizeof(*token));
		if (token == NULL) {
			ret = ENOMEM;
		} else {
			memset(token, 0, sizeof(*token));
			ret = build_token_into(token, obj);
			if (ret != 0) {
				token_free(token);
			}
		}
	} else {
		verbose(FIREHOSE, "%s(): json tokener reported: %s\n",
			__func__, json_tokener_error_desc(jerr));
	}

	json_object_put(obj);

	if (ret == 0) {
		*tokenp = token;
	}

	return ret;
}

void token_free(struct access_token *token)
{
	free(token->access_token);
	free(token);
}
