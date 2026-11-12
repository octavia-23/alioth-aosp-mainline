#include "tiny_sys.h"

struct ml_timespec {
    long tv_sec;
    long tv_nsec;
};

int main_c(int argc, char **argv, char **envp) {
    (void)argc;
    (void)argv;
    (void)envp;
    struct ml_timespec ts;
    ts.tv_sec = 3600;
    ts.tv_nsec = 0;
    while (1) {
        sys_call2(__NR_nanosleep, (long)&ts, 0);
    }
    return 0;
}

void _start(void) {
    main_c(0, (char **)0, (char **)0);
}
