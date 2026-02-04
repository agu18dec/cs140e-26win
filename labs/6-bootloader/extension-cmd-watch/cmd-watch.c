// simple program to run a command when any file that is "interesting" in a directory
// changes.
// e.g.,
//      cmd-watch make
// will run make at each change.
//
// This should use the scandir similar to how you did `find_ttyusb`
//
// key part will be implementing two small helper functions (useful-examples/ will be
// helpful):
//  - static int pid_clean_exit(int pid);
//  - static void run(char *argv[]);
//
#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>

#include "libunix.h"

#define _SVID_SOURCE
#include <dirent.h>

// return 1 if <name> matches any suffix in <suffixes>.
// note: we treat entries not starting with '.' as exact filenames (e.g., "Makefile").
static int is_interesting(const char *name, char *suffixes[]) {
    for(int i = 0; suffixes[i]; i++) {
        const char *s = suffixes[i];
        if(s[0] == '.') {
            if(suffix_cmp(name, s))
                return 1;
        } else {
            if(strcmp(name, s) == 0)
                return 1;
        }
    }
    return 0;
}

// scan the files in "./" (you can extend this) for those
// that match the suffixes in <suffixes> and check  if any
// have changed since the last time.
int check_activity(void) {
    char *suffixes[] = { ".c", ".h", ".S", "Makefile", 0 };
    const char *dirname = ".";
    int changed_p = 0;

    static time_t last_mtime;   // store last modification time.

    struct dirent **namelist = 0;
    int n = scandir(dirname, &namelist, 0, alphasort);
    if(n < 0)
        sys_die(scandir, "scandir(%s) failed", dirname);

    time_t max_mtime = last_mtime;

    for(int i = 0; i < n; i++) {
        const char *name = namelist[i]->d_name;

        // skip "." and ".."
        if(strcmp(name, ".") == 0 || strcmp(name, "..") == 0) {
            free(namelist[i]);
            continue;
        }

        if(!is_interesting(name, suffixes)) {
            free(namelist[i]);
            continue;
        }

        // stat the file to get modification time
        struct stat st;
        if(stat(name, &st) < 0) {
            // file might have disappeared between scandir and stat; ignore.
            free(namelist[i]);
            continue;
        }

        if(st.st_mtime > max_mtime)
            max_mtime = st.st_mtime;

        free(namelist[i]);
    }

    free(namelist);

    if(last_mtime == 0) {
        last_mtime = max_mtime;
        return 1;
    }

    if(max_mtime > last_mtime) {
        changed_p = 1;
        last_mtime = max_mtime;
    }

    return changed_p;
}

// synchronously wait for <pid> to exit.  returns 1 if it exited
// cleanly (via exit(0)), 0 otherwise.
static int pid_clean_exit(int pid) {
    int status;

    int r;
    do {
        r = waitpid(pid, &status, 0);
    } while(r < 0 && errno == EINTR);

    if(r < 0)
        sys_die(waitpid, "waitpid(%d) failed", pid);

    if(WIFEXITED(status) && WEXITSTATUS(status) == 0)
        return 1;

    return 0;
}

// simple helper to print null terminated vector of strings.
static void print_argv(char *argv[]) {
    assert(argv[0]);

    fprintf(stderr, "about to execute this =<%s ", argv[0]);
    for(int i =1; argv[i]; i++)
        fprintf(stderr, " %s", argv[i]);
    fprintf(stderr, ">\n");
}


// fork/exec <argv> and wait for the result: print an error
// and exit if the kid crashed or exited with an error (a non-zero
// exit code).
static void run(char *argv[]) {
    assert(argv && argv[0]);

    print_argv(argv);

    int pid = fork();
    if(pid < 0)
        sys_die(fork, "fork failed");

    if(pid == 0) {
        execvp(argv[0], argv);
        sys_die(execvp, "execvp failed for %s", argv[0]);
        exit(1);
    }

    // parent: wait and check exit status
    int status;
    int r;
    do {
        r = waitpid(pid, &status, 0);
    } while(r < 0 && errno == EINTR);

    if(r < 0)
        sys_die(waitpid, "waitpid failed");

    if(WIFEXITED(status)) {
        int code = WEXITSTATUS(status);
        if(code != 0)
            die("cmd-watch: command exited with status=%d\n", code);
        return;
    }

    if(WIFSIGNALED(status)) {
        int sig = WTERMSIG(status);
        die("cmd-watch: command killed by signal=%d\n", sig);
    }

    die("cmd-watch: command exited abnormally\n");
}

int main(int argc, char *argv[]) {
    if(argc < 2)
        die("cmd-watch: not enough arguments\n");

    char **cmd_argv = &argv[1];

    run(cmd_argv);

    while(1) {
        if(check_activity()) {
            run(cmd_argv);
        } else {
            usleep(250 * 1000);
        }
    }

    return 0;
}
