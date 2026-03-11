#include "rpi.h"
#include "pi-sd.h"
#include "mbr.h"

mbr_t *mbr_read() { // read master boot record from SD card and return pointer to it
  // Be sure to call pi_sd_init() before calling this function!

  // TODO: Read the MBR into a heap-allocated buffer.  Use `pi_sd_read` or
  // `pi_sec_read` to read 1 sector from LBA 0 into memory.
  mbr_t *mbr = kmalloc(sizeof *mbr);
  assert(mbr);
  assert(sizeof *mbr == NBYTES_PER_SECTOR); /// MBR is 1 sector

  int n = pi_sd_read(mbr, 0, 1); // disk read 1 sector into mbr starting at lba 0
  assert(n == 1);

  mbr_check(mbr); // valid MBR
  return mbr;
}
