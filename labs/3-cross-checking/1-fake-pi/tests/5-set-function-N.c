#include <stdlib.h>

#include "rpi.h"
#include "fake-pi.h"

#include <stdio.h>

#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>


static int exit_code(int pid) {
    int status;
    if(waitpid(pid, &status, 0) < 0)
        panic("waitpid failed\n");
    if(!WIFEXITED(status))
        return 1;
    return WEXITSTATUS(status);
}

void fork_gpio_set_function(unsigned pin, unsigned func) {
    output("TRACE: about to set bad fn: pin=%d, func=%d\n", pin,func);

    int pid;

    // child
    if(!(pid = fork()))  {
        gpio_set_function(pin, func);
        fflush(stdout);
        exit(0);
    }

    int ret = exit_code(pid);
    // we just detect and return: you could panic.
    if(ret != 0) {
        output("TRACE: TEST crashed: exit value=%d\n", ret);
        fflush(stdout);
    }
}

void test_gpio_set_function(int ntrials) {
    printf("testing: <%s>\n", __FUNCTION__);
    // test pins 0..32, then a bunch of fake_random.
    for(int pin = 0; pin < 32; pin++)  {
        for(int func = 0;  func < 16; func++) {
            fork_gpio_set_function(pin, func);
            fork_gpio_set_function(fake_random(), func);
        }
    }
#if 0
    for(int i = 0; i < ntrials; i++)
        fork_gpio_set_function(fake_random(), fake_random()%6);
#endif
}

void notmain(void) {
#   define N 8
    test_gpio_set_function(N);
}
