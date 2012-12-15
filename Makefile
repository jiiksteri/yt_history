PROG = yt_history

OBJS = conf.o reply.o https.o auth.o main.o

CFLAGS = -Wall $(shell pkg-config --cflags libevent)
LDFLAGS = $(shell pkg-config --libs libevent)

.PHONY: all clean

all: $(PROG)
$(PROG): $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $(OBJS)

clean:
	$(RM) $(PROG) $(OBJS)

