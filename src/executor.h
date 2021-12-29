#ifndef __EXECUTOR_H_
#define __EXECUTOR_H_

#include <time.h>
struct timespec pexec_timeout = {
    .tv_sec = 0,
    .tv_nsec = 5000000
};

#endif