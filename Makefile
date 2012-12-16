PROG = yt_history

OBJS = conf.o token.o reply.o https.o auth.o main.o

CFLAGS = -g -Wall $(shell pkg-config --cflags libevent_openssl libssl json)
LDFLAGS = $(shell pkg-config --libs libevent_openssl libssl json)

.PHONY: all clean

all: $(PROG)
$(PROG): $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $(OBJS)

clean:
	$(RM) $(PROG) $(OBJS)

