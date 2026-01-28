#include <assert.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include "libunix.h"

// allocate buffer, read entire file into it, return it.   
// buffer is zero padded to a multiple of 4.
//
//  - <size> = exact nbytes of file.
//  - for allocation: round up allocated size to 4-byte multiple, pad
//    buffer with 0s. 
//
// fatal error: open/read of <name> fails.
//   - make sure to check all system calls for errors.
//   - make sure to close the file descriptor (this will
//     matter for later labs).
// 
void *read_file(unsigned *size, const char *name) {
    // How: 
    //    - use stat() to get the size of the file.
    //    - round up to a multiple of 4.
    //    - allocate a buffer
    //    - zero pads to a multiple of 4.
    //    - read entire file into buffer (read_exact())
    //    - fclose() the file descriptor
    //    - make sure any padding bytes have zeros.
    //    - return it. 
    struct stat st;
    if (stat(name, &st) < 0) // stat accepts this structure as struct
        sys_die(stat, "stat failed on %s", name); // demand.h
    unsigned file_size = (unsigned)st.st_size;
    *size = file_size;
    unsigned rounded_size = pi_roundup(file_size, 4);
    char *buffer = calloc(1, rounded_size); // zero fills
    if (!buffer)
        sys_die(calloc, "calloc failed allocating %u bytes for %s", rounded_size, name);

    int fd = open(name, O_RDONLY); // needs to be >= 0 for read_exact, -1 o/w
    if (fd < 0)
        sys_die(open, "open failed on %s", name);
    
    if (file_size)
        read_exact(fd, buffer, file_size);

    close_nofail(fd);
    return buffer;
}
