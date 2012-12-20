
TEST_OBJS = suite_feed.o suite_store.o run_tests.o
PROD_OBJS = feed.o store.o

CFLAGS = -g -D_GNU_SOURCE -DTEST -Wall -Werror -I../ $(shell pkg-config --cflags libevent_openssl expat)
LDFLAGS = -lcunit $(shell pkg-config --libs libevent_openssl expat)

.PHONY: clean all test

all: test

test: $(PROD_OBJS) $(TEST_OBJS) run_tests
	./run_tests

run_tests: $(PROD_OBJS) $(TEST_OBJS)

clean:
	$(RM) $(PROD_OBJS) $(TEST_OBJS) run_tests

suite_%.o: suite_%.c

%.o: ../%.c
	$(CC) $(CFLAGS) -c -o $@ $<


