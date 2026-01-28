// engler, cs140e: your code to find the tty-usb device on your laptop.
#include <assert.h>
#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <errno.h>

#include "libunix.h"

#define _SVID_SOURCE
#include <dirent.h>
static const char *ttyusb_prefixes[] = {
    "ttyUSB",	// linux
    "ttyACM",   // linux
    "cu.SLAB_USB", // mac os
    "cu.usbserial", // mac os
    // if your system uses another name, add it.
	0
};

static int filter(const struct dirent *d) {
    // scan through the prefixes, returning 1 when you find a match.
    // 0 if there is no match.
    for (const char **p = ttyusb_prefixes; *p; p++) {
        // NOTE: in this codebase prefix_cmp is used as a boolean predicate
        // (see my-install.c), so it returns nonzero on match.
        if (prefix_cmp(d->d_name, *p))
            return 1;
    }
    return 0;
}

// find the TTY-usb device (if any) by using <scandir> to search for
// a device with a prefix given by <ttyusb_prefixes> in /dev
// returns:
//  - device name.
// error: panic's if 0 or more than 1 devices.
char *find_ttyusb(void) {
    // use <alphasort> in <scandir>
    // return a malloc'd name so doesn't corrupt.
    struct dirent **namelist = 0;
    int num_files = scandir("/dev", &namelist, filter, alphasort); // keep only the prefix-matching files
    if (num_files < 0)
        panic("scandir(/dev) failed: %s\n", strerror(errno));
    if (num_files == 0)
        panic("no ttyusb devices found in /dev");
    if (num_files != 1)
        panic("found %d ttyusb devices in /dev; expected exactly 1", num_files);

    char *res = strdupf("/dev/%s", namelist[0]->d_name);

    for (int i = 0; i < num_files; i++)
        free(namelist[i]);
    free(namelist);

    return res;
}

static char *sort_files(int newest) {
    struct dirent **namelist = 0; // pointer to array of dirents
    int num_files = scandir("/dev", &namelist, filter, alphasort);
    if (num_files < 0)
        panic("scandir(/dev) failed: %s\n", strerror(errno));
    if (num_files == 0)
        panic("no ttyusb devices found in /dev");

    struct stat new_st;
    int new_i = -1;
    for (int i = 0; i < num_files; i++) {
        char *path = strdupf("/dev/%s", namelist[i]->d_name);

        struct stat st;
        if (stat(path, &st) < 0)
            panic("stat failed on %s: %s\n", path, strerror(errno));

        if (new_i < 0) { // first candidate is best file
            new_i = i;
            new_st = st;
        } else {
            if (newest) { // latest file
                if (st.st_mtime > new_st.st_mtime) {
                    new_i = i;
                    new_st = st;
                }
            } else { // oldest file
                if (st.st_mtime < new_st.st_mtime) {
                    new_i = i;
                    new_st = st;
                }
            }
        }
        free(path);
    }

    char *res = strdupf("/dev/%s", namelist[new_i]->d_name);

    for (int i = 0; i < num_files; i++)
        free(namelist[i]);
    free(namelist);

    return res;
}

// return the most recently mounted ttyusb (the one
// mounted last).  use the modification time
// returned by state.
char *find_ttyusb_last(void) {
    return sort_files(1);
}

// return the oldest mounted ttyusb (the one mounted
// "first") --- use the modification returned by
// stat()
char *find_ttyusb_first(void) {
    return sort_files(0);
}
