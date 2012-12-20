
#include <CUnit/CUnit.h>
#include <CUnit/TestDB.h>

#include <stdio.h>
#include <CUnit/Basic.h>

#include "verbose.h"

static CU_ErrorCode register_suites()
{
	extern CU_SuiteInfo suite_feed;
	extern CU_SuiteInfo suite_store;

	CU_SuiteInfo suites[] = {
		suite_feed,
		suite_store,
		CU_SUITE_INFO_NULL,
	};

	return CU_register_suites(suites);
}

int main(int argc, char **argv)
{
	CU_ErrorCode cerr;
	int failed;

	if (argc > 1 && strcmp(argv[1], "-v") == 0) {
		verbose_adjust_level(+1);
	}

	if ((cerr = CU_initialize_registry() != CUE_SUCCESS)) {
		fprintf(stderr, "CU_initialize_registry() failed: %d\n", cerr);
		return 1;
	}

	if ((cerr = register_suites()) != CUE_SUCCESS) {
		fprintf(stderr, "CU_register_suites() failed: %d\n", cerr);
		return 1;
	}

	if (verbose_adjust_level(0) > NORMAL) {
		CU_basic_set_mode(CU_BRM_VERBOSE);
	}

	if ((cerr = CU_basic_run_tests()) != CUE_SUCCESS) {
		fprintf(stderr, "CU_basic_run_tests() failed: %d\n", cerr);
		return 1;
	}

	if (verbose_adjust_level(0) > NORMAL) {
		CU_basic_show_failures(CU_get_failure_list());
	}

	failed = CU_get_number_of_tests_failed();

	CU_cleanup_registry();

	return failed > 0;
}
