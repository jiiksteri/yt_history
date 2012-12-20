
#include <CUnit/CUnit.h>
#include "test_util.h"

#include "store.h"

#include <event2/http.h>

static int do_session_ensure(struct store *store, struct session **session)
{
	struct evhttp_request *req;
	int ret;

	req = evhttp_request_new(NULL, NULL);
	ret = session_ensure(store, session, req);
	evhttp_request_free(req);
	return ret;
}

static void test_session_count_overflow(void)
{
	/* By experimentation, hcreate_r() with nelem <= 5 always
	 * allocates space for 5 entries. This may probably vary
	 * by system :(
	 */
	int nel = 5;

	struct session *sessions[nel + 1];
	struct store *store;
	int i;

	CU_ASSERT_EQUAL(store_init(&store, nel), 0);

	for (i = 0; i < nel; i++) {
		do_session_ensure(store, &sessions[i]);
		verbose("%s(): ensured %d (%p)\n", __func__, i, sessions[i]);
	}

	CU_ASSERT_EQUAL(ENOMEM, do_session_ensure(store, &sessions[i]));

	for (i = 0; i < nel; i++) {
		session_free(sessions[i]);
		verbose("%s(): freed %d (%p)\n", __func__, i, sessions[i]);
	}

	store_destroy(store);
}


static CU_TestInfo session_tests[] = {
	DECLARE_TESTINFO(test_session_count_overflow),
	CU_TEST_INFO_NULL,
};

const CU_SuiteInfo suite_store[] = {
	{ "session apis", 0, 0, session_tests, },
	CU_SUITE_INFO_NULL,
};
