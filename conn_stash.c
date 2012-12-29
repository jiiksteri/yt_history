#include "conn_stash.h"

#include <event2/bufferevent_ssl.h>

#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <openssl/ssl.h>

#include "verbose.h"

struct conn_slot {

	struct conn_slot *next;
	const char *host;
	int port;

	enum { FREE, IN_USE } status;

	SSL *ssl;
};



struct conn_stash {
	struct event_base *event_base;
	SSL_CTX *ssl_ctx;

	struct conn_slot *conns;

	int no_keepalive;
};

int conn_stash_init(struct conn_stash **stashp, struct event_base *event_base,
		    int no_keepalive)
{

	struct conn_stash *stash;

	SSL_load_error_strings();
	SSL_library_init();

	if ((stash = malloc(sizeof(*stash))) == NULL) {
		return errno;
	}
	memset(stash, 0, sizeof(*stash));


	stash->ssl_ctx = SSL_CTX_new(SSLv23_method());
	if (stash->ssl_ctx == NULL) {
		/* Can't be arsed to do the full error stack parsing
		 * nonsense.
		 */
		free(stash);
		return ENOMEM;
	}

	stash->event_base = event_base;
	stash->no_keepalive = no_keepalive;

	*stashp = stash;
	return 0;
}

static void kill_conn(SSL *ssl)
{
	(void)BIO_set_close(SSL_get_wbio(ssl), 1);
	SSL_free(ssl);
}

void conn_stash_destroy(struct conn_stash *stash)
{
	struct conn_slot *slot, *tmp;

	for (slot = stash->conns; slot;) {
		tmp = slot->next;
		kill_conn(slot->ssl);
		free(slot);
		slot = tmp;
	}

	SSL_CTX_free(stash->ssl_ctx);
	free(stash);
}


static SSL *fresh_conn(struct conn_stash *stash, const char *host, int port)
{
	BIO *bio;
	SSL *ssl;

	bio = BIO_new(BIO_s_connect());
	if (bio == NULL) {
		return NULL;
	}

	BIO_set_nbio(bio, 1);
	BIO_set_conn_hostname(bio, host);
	BIO_set_conn_int_port(bio, &port);

	ssl = SSL_new(stash->ssl_ctx);
	if (ssl == NULL) {
		BIO_free(bio);
		return NULL;
	}

	SSL_set_bio(ssl, bio, bio);
	SSL_connect(ssl);

	return ssl;
}

static int match_stashed(struct conn_slot *slot, const char *host, int port)
{
	return
		slot->status == FREE &&
		slot->port == port &&
		strcmp(slot->host, host) == 0;
}

static SSL *get_stashed_conn(struct conn_stash *stash, const char *host, int port)
{
	SSL *ssl;
	struct conn_slot *slot;

	for (ssl = NULL, slot = stash->conns; !ssl && slot; slot = slot->next) {
		if (match_stashed(slot, host, port)) {
			slot->status = IN_USE;
			ssl = slot->ssl;
		}
	}

	verbose(FIREHOSE, "%s(): stashed conn: %p\n", __func__, ssl);
	return ssl;

}

static void fill_slot_info(struct conn_slot *slot, SSL *ssl)
{
	BIO *bio;

	bio = SSL_get_wbio(ssl);

	slot->host = BIO_get_conn_hostname(bio);
	BIO_ctrl(bio, BIO_C_GET_CONNECT, 3, &slot->port);

	slot->ssl = ssl;
}

static void put_stashed_conn(struct conn_stash *stash, SSL *ssl)
{
	struct conn_slot **slotp;
	for (slotp = &stash->conns; *slotp && (*slotp)->ssl != ssl; slotp = &(*slotp)->next)
		;

	if (*slotp) {
		(*slotp)->status = FREE;
	} else {
		*slotp = malloc(sizeof(**slotp));
		fill_slot_info(*slotp, ssl);
		(*slotp)->next = NULL;
		(*slotp)->status = FREE;
	}
}

static void replace_stashed_conn(struct conn_stash *stash, SSL *old, SSL *new)
{
	struct conn_slot **slotp, **prevp;

	slotp = prevp = &stash->conns;
	while (*slotp && (*slotp)->ssl != old) {
		prevp = slotp;
		slotp = &(*slotp)->next;
	}

	if (*slotp) {
		if (new != NULL) {
			fill_slot_info(*slotp, new);
		} else {
			*prevp = (*slotp)->next;
			free(*slotp);
		}
	}
}


struct bufferevent *conn_stash_get_bev(struct conn_stash *stash,
				       const char *host, int port)
{
	SSL *ssl;
	enum bufferevent_ssl_state bev_ssl_state;

	/* As we don't have keepalive, we'll just return bevs backed
	 * by fresh SSL connections all the time
	 */

	ssl = get_stashed_conn(stash, host, port);
	if (ssl == NULL) {
		ssl = fresh_conn(stash, host, port);
		bev_ssl_state = BUFFEREVENT_SSL_CONNECTING;
	} else {
		bev_ssl_state = BUFFEREVENT_SSL_OPEN;
	}

	if (ssl == NULL) {
		return NULL;
	}

	return bufferevent_openssl_socket_new(stash->event_base, -1, ssl,
					      bev_ssl_state,
					      0);
}

void conn_stash_put_bev(struct conn_stash *stash, struct bufferevent *bev)
{
	SSL *ssl;

	ssl = bufferevent_openssl_get_ssl(bev);

	if (stash->no_keepalive) {
		kill_conn(ssl);
	} else {
		put_stashed_conn(stash, ssl);
	}
	bufferevent_free(bev);
}


int conn_stash_reconnect(struct conn_stash *stash, struct bufferevent **bevp)
{
	SSL *old_ssl;
	SSL *ssl;
	BIO *bio;
	const char *host;
	int port;
	int err;

	ssl = bufferevent_openssl_get_ssl(*bevp);
	bio = SSL_get_wbio(ssl);

	BIO_ctrl(bio, BIO_C_GET_CONNECT, 0, &host);
	BIO_ctrl(bio, BIO_C_GET_CONNECT, 3, &port);

	verbose(NORMAL, "%s(): reconnecting to %s:%d\n",
		__func__, host, port);

	old_ssl = ssl;

	ssl = fresh_conn(stash, host, port);

	replace_stashed_conn(stash, old_ssl, ssl);

	if (ssl != NULL) {
		verbose(VERBOSE, "%s(): reconnected to %s:%d\n",
			__func__, host, port);

		/* get rid of the old bufferevent */
		bufferevent_free(*bevp);

		*bevp = bufferevent_openssl_socket_new(stash->event_base, -1, ssl,
						       BUFFEREVENT_SSL_CONNECTING,
						       0);
		err = 0;
	} else {
		verbose(ERROR, "%s(): could not connect to %s:%d: %d\n",
			__func__, host, port, errno, strerror(errno));
		err = ENOTCONN;
	}

	/* Get rid of the old SSL leftovers */
	(void)BIO_set_close(bio, 1);
	SSL_free(old_ssl);

	return err;
}


int conn_stash_is_keepalive(struct conn_stash *stash)
{
	return !stash->no_keepalive;
}
