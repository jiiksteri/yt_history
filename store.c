#include "store.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <search.h>

#define SESSION_COOKIE_NAME "YT_HISTORY_SESSION"

struct node {
	struct node *next;
	struct node *prev;

	char data[];
};

static void tangle_node(struct node **head, struct node *node)
{
	if (*head) {
		(*head)->prev = node;
	}
	node->next = *head;
	*head = node;
}

static void untangle_node(struct node **head, struct node *node)
{
	if (node == *head) {
		*head = node->next;
	}
	if (node->next != NULL) {
		node->next->prev = node->prev;
	}

	if (node->prev != NULL) {
		node->prev->next = node->next;
	}
}

typedef void (*node_dtor_fn)(void *);

static void nodelist_free(struct node *node, node_dtor_fn dtor)
{
	struct node *tmp = node;

	while (node) {
		tmp = node->next;
		dtor(node);
		node = tmp;
	}
}


static const char *kvnode_key(struct node *node)
{
	return node->data + sizeof(size_t);
}

static const char *kvnode_value(struct node *node)
{
	return node->data + *(size_t *)node->data;
}

static struct node *alloc_node(size_t sz)
{
	struct node *node;

	node = malloc(sizeof(*node) + sz);
	if (node != NULL) {
		memset(node, 0, sizeof(*node) + sz);
	}
	return node;
}

static struct node *alloc_kvnode(const char *key, const char *value)
{
	struct node *node;
	int klen, vlen;

	klen = strlen(key);
	vlen = strlen(value);

	/* A kvnode stores its data thusly:
	 *
	 *   <valueoffset><key>\0<value>\0
	 *
	 * So key starts at &data[sizeof(size_t)], and value
	 * at &data[*(size_t *)&data]
	 *
	 * We don't have a natural container for these, so
	 * we use the expanding ->data member of node directly
	 *
	 * For sessions themselves the situation is different.
	 * All allocations are embedded inside the struct itself,
	 * so we can embed a struct node in session and have it
	 * as the first member, punning it all the way to hell.
	 */

	node = alloc_node(sizeof(size_t) + klen + 1 + vlen + 1);
	if (node != NULL) {
		memcpy(node->data + sizeof(size_t), key, klen + 1);
		node->data[sizeof(size_t) + klen] = '\0';
		memcpy(node->data + sizeof(size_t) + klen + 1, value, vlen + 1);
		*((size_t *)node->data) = sizeof(size_t) + klen + 1;
	}
	return node;
}

struct store {
	struct hsearch_data sessions;
	unsigned int seed;

	struct session *snodes;
};

struct session {

	struct node node;

	struct store *store;

	char id[20];
	struct hsearch_data keyvals;

	struct node *kvnodes;
};

int store_init(struct store **storep, int nel)
{
	struct store *store;

	store = malloc(sizeof(*store));
	if (store == NULL) {
		return errno;
	}

	memset(store, 0, sizeof(*store));

	/* Yeah it's not very random. Nobody cares. */
	store->seed = (unsigned long)store;

	if (hcreate_r(nel, &store->sessions) == 0) {
		free(store);
		return ENOMEM;
	}

	*storep = store;

	return 0;
}


void store_destroy(struct store *store)
{
	if (store != NULL) {
		hdestroy_r(&store->sessions);
		nodelist_free((struct node *)store->snodes, (node_dtor_fn)session_free);
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
		printf("%s(): Failed to store session: %s\n",
		       __func__, strerror(errno));
		return errno;
	}

	tangle_node((struct node **)&store->snodes, (struct node *)session);

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
	int err;

	session = NULL;
	id = session_id_from_request(req);
	if (id != NULL) {
		session = find_existing_session(store, id);
	}

	err = 0;
	if (session == NULL) {
		session = malloc(sizeof(*session));
		memset(session, 0, sizeof(*session));
		session->store = store;
		if (!hcreate_r(10, &session->keyvals)) {
			free(session);
			return ENOMEM;
		}
		err = store_new_session(store, session);
		if (err == 0) {
			add_set_cookie(req, session->id);
		} else {
			hdestroy_r(&session->keyvals);
			free(session);
		}
	}

	if (err == 0) {
		*sessionp = session;
	}

	return err;
}

void session_free(struct session *session)
{
	if (session != NULL) {
		untangle_node((struct node **)&session->store->snodes,
			      (struct node *)session);
		hdestroy_r(&session->keyvals);
		nodelist_free(session->kvnodes, free);
		/* Technically you'd want to untangle() ->kvnodes,
		 * but that'd require making the nodelist_free()
		 * node destructor callback take the list head as
		 * parameter, with our current f'd up linked list
		 * structure. So no.
		 *
		 * They've been freed, make them unreachable too.
		 */
		session->kvnodes = NULL;
		free(session);
	}
}


int session_set_value(struct session *session, const char *key, const char *value)
{
	ENTRY item;
	ENTRY *found;
	struct node *node;

	node = alloc_kvnode(key, value);
	if (node == NULL) {
		return ENOMEM;
	}

	printf("%s(): kvnode_key(): '%s', kvnode_value(): '%s'\n",
	       __func__, kvnode_key(node), kvnode_value(node));

	item.key = (char *)kvnode_key(node);
	item.data = node;

	if (!hsearch_r(item, ENTER, &found, &session->keyvals)) {
		printf("%s(): Failed to store value\n", __func__);
		free(node);
		return ENOMEM;
	}

	tangle_node(&session->kvnodes, node);

	if (found->key != item.key) {
		untangle_node(&session->kvnodes, found->data);
		free(found->data);
	}

	printf("%s() %s stored '%s'\n", __func__, session->id, item.key);

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

	return found ? kvnode_value(found->data) : NULL;
}
