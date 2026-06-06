// Shared entry point for every test executable (M1.4). The suites contribute their
// cases via static TEST() registration; this just runs them. Flags:
//   --suite <name>     run only that suite (CTest registers one entry per suite)
//   --filter <substr>  run cases whose "suite.name" contains substr
//   --list             print the registered cases and exit
#include "test.h"

int main(int argc, char** argv) { return ::test::run_all(argc, argv); }
