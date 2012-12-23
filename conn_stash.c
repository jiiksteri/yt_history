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
};

int conn_stash_init(struct conn_stash **stashp, struct event_base *event_base)
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

	*stashp = stash;
	return 0;
}

void conn_stash_destroy(struct conn_stash *stash)
{
	struct conn_slot *slot, *tmp;

	for (slot = stash->conns; slot;) {
		tmp = slot->next;
		(void)BIO_set_close(SSL_get_wbio(slot->ssl), 1);
		SSL_free(slot->ssl);
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

static void put_stashed_conn(struct conn_stash *stash, SSL *ssl)
{
	struct conn_slot **slotp;
	for (slotp = &stash->conns; *slotp && (*slotp)->ssl != ssl; slotp = &(*slotp)->next)
		;

	if (*slotp) {
		(*slotp)->status = FREE;
	} else {
		*slotp = malloc(sizeof(**slotp));
		memset(*slotp, 0, sizeof(**slotp));
		(*slotp)->host = BIO_get_conn_hostname(SSL_get_wbio(ssl));
		/* Aw, f it. The following would just return 1. */
		/* (*slotp)->port = BIO_get_conn_int_port(SSL_get_wbio(ssl)); */
		BIO_ctrl(SSL_get_wbio(ssl), BIO_C_GET_CONNECT, 3, &(*slotp)->port);
		(*slotp)->ssl = ssl;
		(*slotp)->status = FREE;
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
	put_stashed_conn(stash, bufferevent_openssl_get_ssl(bev));
	/* (void)BIO_set_close(SSL_get_wbio(bufferevent_openssl_get_ssl(bev)), 1); */
	bufferevent_free(bev);
}
