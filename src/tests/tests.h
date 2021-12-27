/**
 * 
 * Indicates which test to run
 * 
 * 
 */


#define RUN_TEST 1
#define TEST_NUM 1


#if RUN_TEST == 1
    #if TEST_NUM == 1
        #define TEST_1
    #elif TEST_NUM == 2
        #define TEST_2
    #elif TEST_NUM == 3
        #define TEST_3
    #elif TEST_NUM == 4
        #define TEST_4
    #elif TEST_NUM == 5
        #define TEST_5
    #endif
#endif