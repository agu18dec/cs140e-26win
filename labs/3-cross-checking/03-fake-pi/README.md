## Tutorial: Building fake-pi implementations

This directory shows three progressively sophisticated implementations
of "fake-pi" --- ways to run bare-metal r/pi code on Unix without
modification.

**Why this matters:** See `../BACKGROUND.md` for the conceptual
explanation of fake-pi, equivalence checking, and cross-checking.

### Key ideas

Big concepts, not much code:
- You can take code written to run on an r/pi on bare metal and
    redefine primitives so it can run on your laptop.

- By running on your laptop you get memory protection, debugging,
  other tools.

- Even a small amount of code lets you run pi programs on Unix and get
  get the same result.

### Organization

This directory has three progressively sophisticated implementations
and two makefiles:

- **`Makefile`**: Selects which version to use (set `FAKE_VERSION`)
- **`Makefile.pi`**: Compiles for real pi hardware
- **`Makefile.fake-pi`**: Compiles for Unix using selected fake-pi version

Test programs:
- `hello.c`: print hello world
- `null-ptr.c`: demonstrates memory protection (crashes on Unix, succeeds
  on pi)
- `test-assert.c`: tests `assert` macro (requires 1-libpi-fake or 
   2-libpi-fake)
- `test-demand.c`: tests `demand` macro (requires 1-libpi-fake or 
   2-libpi-fake)

---

### 0-libpi-fake: Simplest possible

Simplest possible "fake pi"  where we just redefine `printk` and
`clean_reboot` in a private rpi.h  (`0-libpi-fake/rpi.h`) and include
it into a pi program.  Even this trivial hack is enough so you can
run pi programs on your laptop with no changes.

**Files:**
- `0-libpi-fake/rpi.h`: Local header that redefines `printk` 
  and `clean_reboot` as macros.  
- `0-libpi-fake/libpi-fake.c`: Just calls `notmain()` from Unix `main()`.
- `0-libpi-fake/Makefile`: Builds the library.

**What it does:**
- Redefines `printk` as `fprintf(stderr, ...)`
- Redefines `clean_reboot` as inline function that prints "DONE!!!" and 
  exits
- No connection to real libpi headers

**Limitations:**
- Pollutes namespace with system includes (`<stdio.h>`, `<stdlib.h>`)
- Not using real `rpi.h` so can get different behavior
- No access to libpi macros like `assert`, `demand`, `debug`

**Try it:**
```bash
% cd PRELAB/03-fake-pi
% make fake-pi    # Runs hello.c and null-ptr.c on Unix
% make pi         # Runs same programs on real pi
```

---

### 1-libpi-fake: Uses real libpi headers

This version starts getting a bit more real.  The big change is that we
use the raw `rpi.h` header file from the libpi directory at the top of
the class repo (`libpi/include/rpi.h`).


**Files:**
- Uses `libpi/include/rpi.h` (the real one!)
- `1-libpi-fake/libpi-fake.c`: Implements `printk` and `clean_reboot` as 
  real functions
- `1-libpi-fake/Makefile`: Adds include paths to real libpi

**What it does:**
- Uses the actual `rpi.h` from libpi, reducing chance of behavioral 
  differences
- Gives access to macros from `libpi/libc/demand.h`: `assert`, `demand`, 
  `debug`
- Can now run test programs that use these macros

**Try it:**
```bash
% cd PRELAB/03-fake-pi
# Edit Makefile, set: FAKE_VERSION = 1-libpi-fake
# Uncomment: PI_PROGS += test-demand.c test-assert.c
% make fake-pi    # Now runs all four programs
% make pi         # Same programs on real pi
```

---

### 2-libpi-fake: Uses actual libpi source

Now we take a big step and start including much lower level files from
libpi.  Again, the interesting thing here is that these files were written
to run on the pi and yet if we implement the right low level interfaces
we can fake them out and run them on unix without any modification.


**Files:**
- Pulls in real libpi source: `printk.c`, `putk.c`, `putchar.c`, `clean-reboot.c`
- `2-libpi-fake/libpi-fake.c`: Only implements low-level primitives:
  - `uart_putc`: print to stdout
  - `uart_flush_tx`: flush stdout
  - `delay_ms`: do nothing
  - `rpi_reboot`: exit
- `2-libpi-fake/Makefile`: Uses `VPATH` to compile files from libpi

**What it does:**
- Runs actual libpi code on Unix!
- Only needs to implement the low-level UART and timer primitives
- Shows that most libpi code is portable if you implement the right interface

**Key insight:**
These files were written to run on the pi and yet if we implement the
right low level interfaces we can fake them out and run them on Unix
without any modification. The fact we can use them without alteration
in a vastly different context is worth thinking about off and on across
the quarter.

**Try it:**
```bash
% cd PRELAB/03-fake-pi
# Edit Makefile, set: FAKE_VERSION = 2-libpi-fake
% make fake-pi    # Uses real libpi source files
% make pi         # Same result on real pi
```

---

### What to learn from this

1. **Progressive refinement**: Start simple (0), add real headers (1), pull
   in real source (2)

2. **Interface matters**: If you isolate hardware dependencies behind a clean
   interface (`uart_putc`, etc.), most code becomes portable

3. **Minimal stubs**: You only need to implement a tiny amount of
   platform-specific code to make everything work

4. **Testing strategy**: For more complex projects, build a fake
   implementation early. It makes testing much easier.

There's a lot more that can be done to do real emulation/simulation. This
is just a "hello world" level view. With that said, it's enough to do some
interesting stuff in today's lab.
