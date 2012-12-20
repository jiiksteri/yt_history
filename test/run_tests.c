
#include <CUnit/CUnit.h>
#include <CUnit/TestDB.h>

#include <stdio.h>
#include <CUnit/Basic.h>

static CU_ErrorCode register_suites()
{
	extern CU_SuiteInfo suite_feed;

	CU_SuiteInfo suites[] = {
		suite_feed,
		CU_SUITE_INFO_NULL,
	};

	return CU_register_suites(suites);
}

int main(void)
{
	CU_ErrorCode cerr;
	int failed;

	if ((cerr = CU_initialize_registry() != CUE_SUCCESS)) {
		fprintf(stderr, "CU_initialize_registry() failed: %d\n", cerr);
		return 1;
	}

	if ((cerr = register_suites()) != CUE_SUCCESS) {
		fprintf(stderr, "CU_register_suites() failed: %d\n", cerr);
		return 1;
	}

	CU_basic_set_mode(CU_BRM_VERBOSE);
	if ((cerr = CU_basic_run_tests()) != CUE_SUCCESS) {
		fprintf(stderr, "CU_basic_run_tests() failed: %d\n", cerr);
		return 1;
	}

	CU_basic_show_failures(CU_get_failure_list());

	failed = CU_get_number_of_tests_failed();

	CU_cleanup_registry();

	return failed > 0;
}
