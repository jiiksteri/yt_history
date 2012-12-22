#include "conn_stash.h"

#include <event2/bufferevent_ssl.h>

#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <openssl/ssl.h>


struct conn_stash {
	struct event_base *event_base;
	SSL_CTX *ssl_ctx;
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


struct bufferevent *conn_stash_get_bev(struct conn_stash *stash,
				       const char *host, int port)
{
	SSL *ssl;

	/* As we don't have keepalive, we'll just return bevs backed
	 * by fresh SSL connections all the time
	 */

	if ((ssl = fresh_conn(stash, host, port)) == NULL) {
		return NULL;
	}

	return bufferevent_openssl_socket_new(stash->event_base, -1, ssl,
					      BUFFEREVENT_SSL_CONNECTING,
					      BEV_OPT_CLOSE_ON_FREE);
}

void conn_stash_put_bev(struct conn_stash *stash, struct bufferevent *bev)
{
	(void)BIO_set_close(SSL_get_wbio(bufferevent_openssl_get_ssl(bev)), 1);
	bufferevent_free(bev);
}
