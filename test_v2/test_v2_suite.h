#ifndef TEST_V2_SUITE_H_
#define TEST_V2_SUITE_H_

typedef void (*Test_Suite_Fn)(int *passed, int *failed);

void run_lexer_v2_tests(int *passed, int *failed);
void run_parser_v2_tests(int *passed, int *failed);
void run_evaluator_v2_tests(int *passed, int *failed);

#endif // TEST_V2_SUITE_H_