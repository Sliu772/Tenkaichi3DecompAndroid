/*
 * iOS: the program's entry and what is needed to find out why it does not run. Host code (hard float, 8-byte
 * pointers; android.py compiles this file without ptr32 and the soft-float ABI).
 *
 *   - Before anything else (constructor priority 101, ahead of plat_mem.c's Port_MapMemory) stdout and stderr go to
 *     bt3_log.txt in the app's Documents folder (visible in the Files app) and the working folder becomes Documents.
 *   - The log says where the image is, how large, and whether the address ranges the game's 4-byte pointers need
 *     can be had: they are OFFSETS from the Mach-O header (p32rebase.py), so everything the game addresses must lie in
 *     [header, header + 4 GB).
 *   - A signal handler writes the fault, the registers that matter and a backtrace to the same log.
 *   - main(): SDL's UIKit entry (SDL_RunApp) calls the game's main() (renamed __real_main in src/main.c) through
 *     plat_mem.c's __wrap_main.
 */
#ifdef __APPLE__
#include <errno.h>
#include <execinfo.h>
#include <fcntl.h>
#include <mach-o/dyld.h>
#include <mach-o/ldsyms.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/ucontext.h>
#include <unistd.h>

extern int __wrap_main(int argc, char **argv);
extern int SDL_RunApp(int argc, char *argv[], int (*mainFunction)(int, char *[]), void *reserved);

#define WINDOW 0x100000000ULL

static uintptr_t hdr(void) {
    return (uintptr_t)&_mh_execute_header;
}

/* End of the image as an offset from the header: the highest end of any segment but __PAGEZERO. */
static uint64_t image_end(void) {
    const struct mach_header_64 *mh = (const struct mach_header_64 *)&_mh_execute_header;
    const uint8_t *p = (const uint8_t *)(mh + 1);
    uint64_t end = 0;
    uint32_t i;

    for (i = 0; i < mh->ncmds; i++) {
        const struct load_command *lc = (const struct load_command *)p;
        if (lc->cmd == LC_SEGMENT_64) {
            const struct segment_command_64 *sc = (const struct segment_command_64 *)p;
            if (strcmp(sc->segname, "__PAGEZERO") != 0 && sc->vmaddr + sc->vmsize - 0x100000000ULL > end) {
                end = sc->vmaddr + sc->vmsize - 0x100000000ULL;
            }
        }
        p += lc->cmdsize;
    }
    return end;
}

/* The lowest offset (from `from`, in 16 MB steps) at which `size` bytes can be mapped inside the window; 0: none. */
static uint64_t find_window(uint64_t from, uint64_t size) {
    uint64_t off;

    for (off = (from + 0xFFFFFFULL) & ~0xFFFFFFULL; off + size <= WINDOW; off += 0x1000000ULL) {
        void *p = mmap((void *)(hdr() + off), (size_t)size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
        if (p == MAP_FAILED) {
            continue;
        }
        munmap(p, (size_t)size);
        if ((uintptr_t)p == hdr() + off) {
            return off;
        }
    }
    return 0;
}

static void crash(int sig, siginfo_t *si, void *ctx) {
    ucontext_t *uc = (ucontext_t *)ctx;
    void *bt[48];
    int n;

    fprintf(stderr, "\nbt3: signal %d, address %p, pc 0x%llx (offset 0x%llx), lr 0x%llx, sp 0x%llx\n", sig, si->si_addr,
            (unsigned long long)__darwin_arm_thread_state64_get_pc(uc->uc_mcontext->__ss),
            (unsigned long long)(__darwin_arm_thread_state64_get_pc(uc->uc_mcontext->__ss) - hdr()),
            (unsigned long long)__darwin_arm_thread_state64_get_lr(uc->uc_mcontext->__ss),
            (unsigned long long)__darwin_arm_thread_state64_get_sp(uc->uc_mcontext->__ss));
    n = backtrace(bt, 48);
    fflush(stderr);
    backtrace_symbols_fd(bt, n, 2);
    _exit(128 + sig);
}

static void install_crash_handler(void) {
    static uint8_t altstack[1 << 16];
    stack_t ss;
    struct sigaction sa;
    static const int sigs[] = {SIGSEGV, SIGBUS, SIGILL, SIGABRT, SIGTRAP, SIGFPE};
    unsigned i;

    ss.ss_sp = altstack;
    ss.ss_size = sizeof(altstack);
    ss.ss_flags = 0;
    sigaltstack(&ss, NULL);
    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = crash;
    sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
    sigemptyset(&sa.sa_mask);
    for (i = 0; i < sizeof(sigs) / sizeof(sigs[0]); i++) {
        sigaction(sigs[i], &sa, NULL);
    }
}

__attribute__((constructor(101))) static void Ios_Early(void) {
    const char *home = getenv("HOME");
    char dir[1024], path[1100];
    uint64_t end, o;
    static const struct { const char *what; uint64_t size; } want[] = {
        {"game heap (32 MB)", 0x2000000ULL},        {"port's low arena (192 MB)", 0xC000000ULL},
        {"game thread stack (16 MB)", 0x1000000ULL}, {"256 MB", 0x10000000ULL},
        {"512 MB", 0x20000000ULL},                   {"1 GB", 0x40000000ULL},
    };
    unsigned i;

    if (home != NULL) {
        snprintf(dir, sizeof(dir), "%s/Documents", home);
        mkdir(dir, 0755);
        snprintf(path, sizeof(path), "%s/bt3_log.txt", dir);
        int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd >= 0) {
            dup2(fd, 1);
            dup2(fd, 2);
            close(fd);
        }
        if (chdir(dir) != 0) {
        }
    }
    setvbuf(stdout, NULL, _IOLBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);
    install_crash_handler();
    end = image_end();
    fprintf(stderr, "bt3 iOS: header %p, slide 0x%lx, image 0x%llx bytes (offsets 0..0x%llx)\n", (void *)hdr(),
            (long)_dyld_get_image_vmaddr_slide(0), (unsigned long long)end, (unsigned long long)end);
    fprintf(stderr, "bt3 iOS: home %s\n", home != NULL ? home : "(none)");
    for (i = 0; i < sizeof(want) / sizeof(want[0]); i++) {
        o = find_window(end, want[i].size);
        if (o != 0) {
            fprintf(stderr, "bt3 iOS: %-28s fits in the window at offset 0x%llx\n", want[i].what, (unsigned long long)o);
        } else {
            fprintf(stderr, "bt3 iOS: %-28s DOES NOT FIT in [header, header + 4 GB)\n", want[i].what);
        }
    }
}

static int game_entry(int argc, char *argv[]) {
    fprintf(stderr, "bt3 iOS: SDL started the app, calling the game\n");
    return __wrap_main(argc, argv);
}

int main(int argc, char **argv) {
    fprintf(stderr, "bt3 iOS: main, starting SDL\n");
    return SDL_RunApp(argc, argv, game_entry, NULL);
}
#endif
