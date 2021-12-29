/**
 * 
 * Indicates which test to run
 * 
 * 
 */

#ifndef __LTEST_H_
#define __LTEST_H_

#include "../main.h"


#if RUN_LTEST == 1
    #if TEST_LNUM == 1
        #define LTEST_1
    #elif TEST_LNUM == 2
        #define LTEST_2
    #elif TEST_LNUM == 3
        #define LTEST_3
    #elif TEST_LNUM == 4
        #define LTEST_4
    #elif TEST_LNUM == 5
        #define LTEST_5
    #elif TEST_LNUM == 6
        #define LTEST_6
    #elif TEST_LNUM == 7
        #define LTEST_7
    #endif
#endif

#endif