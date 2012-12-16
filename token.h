#ifndef TOKEN_H__INCLUDED
#define TOKEN_H__INCLUDED

#include <event2/buffer.h>

struct access_token {
	char *access_token;
	int expires_in;
};

int token_parse_json(struct access_token **tokenp, struct evbuffer *buf);

void token_free(struct access_token *token);

#endif
