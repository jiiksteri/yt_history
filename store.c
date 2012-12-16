#include "store.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <search.h>

#define SESSION_COOKIE_NAME "YT_HISTORY_SESSION"

struct store {
	struct hsearch_data sessions;
	unsigned int seed;
};

struct session {
	char id[20];
	struct hsearch_data keyvals;
};

int store_init(struct store **storep)
{
	struct store *store;

	store = malloc(sizeof(*store));
	if (store == NULL) {
		return errno;
	}

	memset(store, 0, sizeof(*store));

	/* Yeah it's not very random. Nobody cares. */
	store->seed = (unsigned long)store;

	if (hcreate_r(10, &store->sessions) == 0) {
		free(store);
		return ENOMEM;
	}

	*storep = store;

	return 0;
}


void store_destroy(struct store *store)
{
	if (store != NULL) {
		/* We should probably figure out how
		 * to clear it first... :P
		 */
		hdestroy_r(&store->sessions);

		free(store);
	}
}


static const char *session_id_from_request(struct evhttp_request *req)
{
	const char *cookie;
	const char *id = NULL;
	int nlen;

	cookie = evhttp_find_header(evhttp_request_get_input_headers(req), "Cookie");
	printf("%s(): Cookie is %s\n", __func__, cookie);

	nlen = strlen(SESSION_COOKIE_NAME);
	if (cookie != NULL && strncmp(SESSION_COOKIE_NAME, cookie, nlen) == 0) {
		/* It's oursssess */
		nlen = strlen(SESSION_COOKIE_NAME);
		if (cookie[nlen] == '=') {
			/* The cookie's not going away before we've done
			 * the lookup, so no need to copy it.
			 */
			id = &cookie[nlen+1];
		}
	}

	return id;
}

static struct session *find_existing_session(struct store *store, const char *id)
{
	ENTRY needle;
	ENTRY *found;

	struct session *session = NULL;

	needle.key = (char *)id;
	if (hsearch_r(needle, FIND, &found, &store->sessions)) {
		printf("%s(): Found existing session with id %s\n", __func__, id);
		session = found->data;
	} else {
		printf("%s(): Existing session with id %s not found."
		       " Throwing it away\n", __func__, id);
	}

	return session;
}

static int store_new_session(struct store *store, struct session *session)
{
	ENTRY item;
	ENTRY *found;

	snprintf(session->id, sizeof(session->id), "%x%x%x%x",
		 rand_r(&store->seed), rand_r(&store->seed),
		 rand_r(&store->seed), rand_r(&store->seed));

	item.key = session->id;
	item.data = session;
	found = NULL;
	if (!hsearch_r(item, ENTER, &found, &store->sessions)) {
		printf("%s(): Failed to store session\n", __func__);
		return ENOMEM;
	}

	if (found->data != item.data) {
		printf("%s(): Uhm. Session already existed ((%p,%p), trying to insert (%p,%p)\n",
		       __func__,
		       found->key, found->data,
		       item.key, item.data);
		session_free(found->data);
	}

	return 0;

}

static void add_set_cookie(struct evhttp_request *req, const char *id)
{
	char value[128];

	snprintf(value, sizeof(value), "%s=%s", SESSION_COOKIE_NAME, id);
	evhttp_add_header(evhttp_request_get_output_headers(req),
			  "Set-Cookie", value);
	printf("%s(): added Set-Cookie %s\n", __func__, value);

}

int session_ensure(struct store *store, struct session **sessionp, struct evhttp_request *req)
{
	const char *id;
	struct session *session;

	session = NULL;
	id = session_id_from_request(req);
	if (id != NULL) {
		session = find_existing_session(store, id);
	}

	if (session == NULL) {
		session = malloc(sizeof(*session));
		memset(session, 0, sizeof(*session));
		if (!hcreate_r(10, &session->keyvals)) {
			free(session);
			return ENOMEM;
		}
		store_new_session(store, session);
		add_set_cookie(req, session->id);
	}

	*sessionp = session;
	return 0;
}

void session_free(struct session *session)
{
	if (session != NULL) {
		free(session);
	}
}


int session_set_value(struct session *session, const char *key, const char *value)
{
	ENTRY item;
	ENTRY *found;

	if ((item.key = strdup(key)) == NULL) {
		return ENOMEM;
	}

	if ((item.data = strdup(value)) == NULL) {
		free(item.key);
		return ENOMEM;
	}

	if (!hsearch_r(item, ENTER, &found, &session->keyvals)) {
		printf("%s(): Failed to store value\n", __func__);
		free(item.data);
		free(item.key);
		return ENOMEM;
	}

	if (found->key != item.key) {
		free(found->key);
	}

	if (found->data != item.data) {
		free(found->data);
	}
	printf("%s() %s stored '%s'\n", __func__, session->id, key);

	return 0;
}

const char *session_get_value(struct session *session, const char *key)
{
	ENTRY item;
	ENTRY *found = NULL;

	item.key = (char *)key;
	if (!hsearch_r(item, FIND, &found, &session->keyvals)) {
		printf("%s(): %s hsearch_r() failed\n", __func__, key);
	}

	return found ? found->data : NULL;
}
