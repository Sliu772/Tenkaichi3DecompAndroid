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
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/ucontext.h>
#include <unistd.h>
#include <pthread.h>
#include <mach/mach.h>
#include <os/proc.h> /* os_proc_available_memory */

#include "ios_install.h" /* ios_prepare_data(): installs the game's data from an .iso in Documents */

extern int __wrap_main(int argc, char **argv);
extern int SDL_RunApp(int argc, char *argv[], int (*mainFunction)(int, char *[]), void *reserved);
extern bool SDL_SetHint(const char *name, const char *value);
extern bool SDL_AddEventWatch(bool (*filter)(void *userdata, void *event), void *userdata);

#define WINDOW 0x100000000ULL

static uintptr_t hdr(void) {
    return (uintptr_t)&_mh_execute_header;
}

/* The game's 4-byte pointer fields, written and read here (host code, ordinary pointers) instead of by the game's code:
 * a pointer parameter that the game stores into a 4-byte field goes in as the low half of its real address (offset + the
 * ASLR slide), whatever the C looks like (BtlTaskList_Create: log 87 still showed parent = group + slide with
 * GAME_ADDR in place). The stored value here is always the offset from the header; 0 stays 0. */
void Port_StorePtr32(void *field, const void *p) {
    uint32_t off = p == NULL ? 0u : (uint32_t)((uintptr_t)p - hdr());

    *(uint32_t *)field = off;
}

void *Port_LoadPtr32(const void *field) {
    uint32_t off = *(const uint32_t *)field;

    return off == 0 ? NULL : (void *)(hdr() + off);
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

/* The game's memory (plat_mem.c): ONE block of `size` bytes inside [header, header + 4 GB), the first that can be had
   above the image (16 MB steps; the system chooses where an address that is only a hint ends up, so the result is
   checked). It stays mapped. NULL: no room. */
void *Ios_MapWindow(uint64_t size) {
    uint64_t off;

    for (off = (image_end() + 0xFFFFFFULL) & ~0xFFFFFFULL; off + size <= WINDOW; off += 0x1000000ULL) {
        void *p = mmap((void *)(hdr() + off), (size_t)size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
        if (p == MAP_FAILED) {
            continue;
        }
        if ((uintptr_t)p == hdr() + off) {
            return p;
        }
        munmap(p, (size_t)size);
    }
    return NULL;
}

/* Bring-up, two layers:
   1. A fault whose address is a game OFFSET used as a raw pointer (below 4 GB, from 0x100000 up, which covers the static
      buffers in .bss at 0x46xxxx): the register
      that holds it gets the header added and the instruction is retried ("FIXRAW"). The same bug class as the ABI
      mismatches (an offset where a host pointer belongs), healed on the spot and logged with its call chain.
   2. Any other fault inside the game's code is logged and its instruction skipped ("SKIP"); a load leaves its
      register as it was.
   Gives up (full dump) after 20000 SKIPs in total or 3000 at the same pc (FIXRAW has its own budget: 5 million per pc).
   Only the first 3 per pc are logged. */
static int skip_fault(int sig, siginfo_t *si, ucontext_t *uc) {
    static int total;
    static uintptr_t seen[256];
    static int cnt[256];
    __typeof__(uc->uc_mcontext->__ss) *ss = &uc->uc_mcontext->__ss;
    uintptr_t pc = (uintptr_t)__darwin_arm_thread_state64_get_pc(*ss);
    uintptr_t lr = (uintptr_t)__darwin_arm_thread_state64_get_lr(*ss);
    uintptr_t lo = hdr(), hi = hdr() + (uintptr_t)image_end();
    uintptr_t fp = (uintptr_t)__darwin_arm_thread_state64_get_fp(*ss);
    uintptr_t sp = (uintptr_t)__darwin_arm_thread_state64_get_sp(*ss);
    uint64_t fa = (uint64_t)(uintptr_t)si->si_addr;
    int i, k, fixed = 0, loud;

    if ((sig != SIGSEGV && sig != SIGBUS) || pc < lo || pc >= hi || total >= 20000) {
        return 0;
    }
    for (i = 0; i < 256 && seen[i] != 0 && seen[i] != pc; i++) {
    }
    if (i == 256) {
        return 0;
    }
    seen[i] = pc;
    if (fa < 0x100000000ULL) {
        for (k = 0; k < 29; k++) {
            uint64_t r = ss->__x[k];
            if (r >= 0x100000ULL && r < 0x20000000ULL && fa >= r && fa - r < 0x10000ULL) {
                ss->__x[k] = r + (uint64_t)lo;
                fixed++;
            }
        }
    }
    if (fixed) {
        /* A healed fault is not a failure: the same call site hands over a raw offset on every call (once per
           polygon in clip_project), so it has its own, much larger budget and does not use up the SKIP budget. */
        static unsigned fixcnt[256];

        if (++fixcnt[i] > 5000000U) {
            return 0;
        }
        loud = fixcnt[i] <= 3;
    } else {
        if (++cnt[i] > 3000) {
            return 0;
        }
        total++;
        loud = cnt[i] <= 3;
    }
    if (loud) {
        fprintf(stderr, "bt3 %s #%d: signal %d addr %p pc offset 0x%llx lr offset 0x%llx chain:", fixed ? "FIXRAW" : "SKIP", total,
                sig, si->si_addr, (unsigned long long)(pc - lo), (unsigned long long)(lr - lo));
        for (k = 0; k < 8 && fp != 0 && (fp & 7) == 0 && fp >= sp && fp - sp < 0x100000; k++) {
            uintptr_t ra = ((uintptr_t *)fp)[1];
            fprintf(stderr, " 0x%llx", (unsigned long long)(ra - lo));
            fp = ((uintptr_t *)fp)[0];
        }
        fprintf(stderr, "\n");
    }
    if (!fixed) {
        __darwin_arm_thread_state64_set_pc_fptr(*ss, (void *)(pc + 4));
    }
    return 1;
}

static void crash(int sig, siginfo_t *si, void *ctx) {
    ucontext_t *uc = (ucontext_t *)ctx;
    void *bt[48];
    int n;

    if (skip_fault(sig, si, uc)) {
        return;
    }

    fprintf(stderr, "\nbt3: signal %d, address %p, pc 0x%llx (offset 0x%llx), lr 0x%llx, sp 0x%llx\n", sig, si->si_addr,
            (unsigned long long)__darwin_arm_thread_state64_get_pc(uc->uc_mcontext->__ss),
            (unsigned long long)(__darwin_arm_thread_state64_get_pc(uc->uc_mcontext->__ss) - hdr()),
            (unsigned long long)__darwin_arm_thread_state64_get_lr(uc->uc_mcontext->__ss),
            (unsigned long long)__darwin_arm_thread_state64_get_sp(uc->uc_mcontext->__ss));
    {
        /* backtrace() below runs on the signal's own stack and says nothing about the faulting code, so also: the
           argument registers, the frame-pointer chain and the stack words that point into the game's image (return
           addresses, as offsets for bt3.map: address in the map = 0x100000000 + offset). */
        __typeof__(uc->uc_mcontext->__ss) *ss = &uc->uc_mcontext->__ss;
        uintptr_t lo = hdr(), hi = hdr() + (uintptr_t)image_end();
        uintptr_t sp = (uintptr_t)__darwin_arm_thread_state64_get_sp(*ss);
        uintptr_t fp = (uintptr_t)__darwin_arm_thread_state64_get_fp(*ss);
        uintptr_t *w;
        int k, shown = 0;

        fprintf(stderr, "regs: x0 0x%llx x1 0x%llx x2 0x%llx x3 0x%llx x19 0x%llx x20 0x%llx x21 0x%llx fp 0x%llx\n",
                (unsigned long long)ss->__x[0], (unsigned long long)ss->__x[1], (unsigned long long)ss->__x[2],
                (unsigned long long)ss->__x[3], (unsigned long long)ss->__x[19], (unsigned long long)ss->__x[20],
                (unsigned long long)ss->__x[21], (unsigned long long)fp);
        for (k = 0; k < 24 && fp != 0 && (fp & 7) == 0 && fp >= sp && fp - sp < 0x100000; k++) {
            uintptr_t ra = ((uintptr_t *)fp)[1];
            fprintf(stderr, "fp chain %d: ra 0x%llx%s 0x%llx\n", k, (unsigned long long)ra, ra >= lo && ra < hi ? " game offset" : "", (unsigned long long)(ra - lo));
            fp = ((uintptr_t *)fp)[0];
        }
        for (w = (uintptr_t *)(sp & ~(uintptr_t)7); (uintptr_t)w < sp + 0x2000 && shown < 40; w++) {
            if (*w >= lo && *w < hi && (*w & 3) == 0) {
                fprintf(stderr, "stack +0x%llx: game offset 0x%llx\n", (unsigned long long)((uintptr_t)w - sp), (unsigned long long)(*w - lo));
                shown++;
            }
        }
    }
    n = backtrace(bt, 48);
    fflush(stderr);
    backtrace_symbols_fd(bt, n, 2);
    _exit(128 + sig);
}

/* Diagnostic: how does the process end when there is no fault message? The three ways that leave no trace in the log
   are caught here: exit() / return from main, a termination signal (SIGTERM, SIGPIPE ...), and the app lifecycle events
   SDL receives from iOS (terminating, low memory, background). SIGKILL (jetsam, watchdog) cannot be caught: if none of
   these lines appears before the log ends, it was SIGKILL. */
static void log_line(const char *s) {
    if (write(2, s, strlen(s)) < 0) {
    }
}

static void on_exit_log(void) {
    void *bt[24];
    int n;

    log_line("bt3 end: exit() or a return from main was called (no signal, not a kill by iOS)\n");
    n = backtrace(bt, 24);
    backtrace_symbols_fd(bt, n, 2);
}

static void on_other_signal(int sig, siginfo_t *si, void *ctx) {
    static int pipes;
    char b[128];
    void *bt[24];
    int n;

    (void)ctx;
    if (sig == SIGPIPE && pipes++ >= 5) {
        return; /* SIGPIPE only ends the process by default: ignored from now on, logged 5 times */
    }
    n = snprintf(b, sizeof(b), "bt3 end: signal %d received (sender pid %d, my pid %d)\n", sig, si != NULL ? (int)si->si_pid : -1, (int)getpid());
    if (write(2, b, (size_t)n) < 0) {
    }
    n = backtrace(bt, 24);
    backtrace_symbols_fd(bt, n, 2);
    if (sig != SIGPIPE) {
        _exit(128 + sig);
    }
}

static bool life_watch(void *userdata, void *event) {
    static int lines;
    uint32_t type = *(uint32_t *)event;
    char b[96];
    int n;

    (void)userdata;
    if ((type >= 0x100 && type < 0x200) && lines++ < 100) { /* SDL3 application events: quit, terminating, low memory, (will) enter background / foreground */
        n = snprintf(b, sizeof(b), "bt3 life: SDL event 0x%x%s\n", type,
                     type == 0x100 ? " (QUIT)" : type == 0x101 ? " (TERMINATING)" : type == 0x102 ? " (LOW MEMORY)" :
                     type == 0x103 ? " (will enter background)" : type == 0x104 ? " (did enter background)" : "");
        if (write(2, b, (size_t)n) < 0) {
        }
    }
    return true;
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
    {
        static const int other[] = {SIGTERM, SIGHUP, SIGQUIT, SIGPIPE, SIGXCPU};
        struct sigaction sb;

        memset(&sb, 0, sizeof(sb));
        sb.sa_sigaction = on_other_signal;
        sb.sa_flags = SA_SIGINFO | SA_ONSTACK;
        sigemptyset(&sb.sa_mask);
        for (i = 0; i < sizeof(other) / sizeof(other[0]); i++) {
            sigaction(other[i], &sb, NULL);
        }
    }
    atexit(on_exit_log);
}

__attribute__((constructor(101))) static void Ios_Early(void) {
    const char *home = getenv("HOME");
    char dir[1024], path[1100];
    uint64_t end, o;
    static const struct { const char *what; uint64_t size; } want[] = {
        {"game window (240 MB)", 0xF000000ULL}, {"512 MB", 0x20000000ULL}, {"1 GB", 0x40000000ULL},
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

/* The renderer asks SDL_GPU for SPIR-V shaders, which only its Vulkan driver takes (the Metal one wants MSL): Vulkan on
   Metal is MoltenVK, packaged next to the program (ios_package.sh). SDL's UIKit loader looks for "libvulkan.dylib" by its
   bare name, which dyld does not find inside an app: it is given the framework's path. */
static void find_moltenvk(void) {
    char exe[1024], lib[1200];
    uint32_t n = sizeof(exe);
    char *slash;

    if (_NSGetExecutablePath(exe, &n) != 0 || (slash = strrchr(exe, '/')) == NULL) {
        return;
    }
    *slash = '\0';
    snprintf(lib, sizeof(lib), "%s/Frameworks/MoltenVK.framework/MoltenVK", exe);
    fprintf(stderr, "bt3 iOS: Vulkan library %s: %s\n", lib, access(lib, R_OK) == 0 ? "found" : "NOT FOUND (no renderer)");
    SDL_SetHint("SDL_VULKAN_LIBRARY", lib);
}

/* Heartbeat (iOS bring-up): a second thread that, every second, looks at where the game thread is (its pc and lr as
   offsets for bt3.map) and how much memory the app uses, and writes one line to the log. If iOS kills the app (the
   watchdog, or for memory: no signal, so no crash report), the last lines say where it was and how large it had grown.
   Only system calls and write(): the game thread is stopped for a moment and may hold a lock of the C library. */
static mach_port_t sGameThread;
extern unsigned gSndMgr; /* the game's sound manager (src/sys/snd.c), a 4-byte pointer: offset from the header */
extern void SpuHeap_FindBlock(void);
extern unsigned gSpuHeapBlocks[]; /* the game's SPU heap table (src/sys/spu_heap.c): 17 x {state, addr, size} */

static void hb_hex(char *out, unsigned long long v) {
    int i;
    for (i = 0; i < 16; i++) {
        out[i] = "0123456789abcdef"[(v >> (60 - i * 4)) & 15];
    }
}

static void hb_hex32(char *out, unsigned v) { /* 8 digits */
    int i;
    for (i = 0; i < 8; i++) {
        out[i] = "0123456789abcdef"[(v >> (28 - i * 4)) & 15];
    }
}

static void hb_dec(char *out, unsigned long long v) { /* 8 digits, zero padded */
    int i;
    for (i = 7; i >= 0; i--) {
        out[i] = (char)('0' + v % 10);
        v /= 10;
    }
}

static void *heartbeat(void *arg) {
    unsigned long long sec = 0;
    struct stat beat_sb;
    /* Stopping the game thread every second to read its registers froze the game at the end of the last three logs
       (the log ends within a second of a beat, with the game still drawing frames). So by default the beat only
       reports memory. To get pc/lr/sp/registers again, create an empty file named bt3_beat_regs in Documents. */
    int beat_regs = stat("bt3_beat_regs", &beat_sb) == 0;
    (void)arg;
    for (;;) {
        arm_thread_state64_t st;
        mach_msg_type_number_t cnt = ARM_THREAD_STATE64_COUNT;
        task_vm_info_data_t vi;
        mach_msg_type_number_t vc = TASK_VM_INFO_COUNT;
        unsigned long long pc = 0, lr = 0, sp = 0, mb = 0, base = (unsigned long long)hdr();
        int ok = 0;
        char line[160] = "bt3 beat t=00000000s pc 0x0000000000000000 lr 0x0000000000000000 sp 0x0000000000000000 mem 00000000 MB\n";

        sleep(sec < 60 ? 1 : 5);
        sec += sec < 60 ? 1 : 5;
        if (beat_regs && thread_suspend(sGameThread) == KERN_SUCCESS) {
            if (thread_get_state(sGameThread, ARM_THREAD_STATE64, (thread_state_t)&st, &cnt) == KERN_SUCCESS) {
                ok = 1;
                pc = (unsigned long long)__darwin_arm_thread_state64_get_pc(st);
                lr = (unsigned long long)__darwin_arm_thread_state64_get_lr(st);
                sp = (unsigned long long)__darwin_arm_thread_state64_get_sp(st);
            }
            thread_resume(sGameThread);
        }
        if (task_info(mach_task_self(), TASK_VM_INFO, (task_info_t)&vi, &vc) == KERN_SUCCESS) {
            mb = (unsigned long long)vi.phys_footprint >> 20;
        }
        /* pc and lr as offsets from the header when inside the image (what bt3.map lists), else the raw address */
        if (pc >= base && pc - base < 0x2000000ULL) pc -= base;
        if (lr >= base && lr - base < 0x2000000ULL) lr -= base;
        hb_dec(line + 11, sec);
        if (ok) {
            hb_hex(line + 26, pc);
            hb_hex(line + 48, lr);
            hb_hex(line + 70, sp);
        }
        hb_dec(line + 91, mb);
        if (write(2, line, strlen(line)) < 0) {
        }
        { /* how much more memory iOS lets the app use before it kills it (0 = at the limit or unknown) */
            char ml[32] = "bt3 avail 00000000 MB\n";
            hb_dec(ml + 10, (unsigned long long)os_proc_available_memory() >> 20);
            if (write(2, ml, strlen(ml)) < 0) {
            }
        }
        if (ok) { /* where a loop stands: the registers, and the SPU heap table when the game is inside SpuHeap_* */
            char rl[400];
            size_t n = 0;
            int r, w;
            static const int regs[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 19, 20, 21, 22, 23, 24};
            memcpy(rl, "bt3 regs x0-12,x19-24", 21);
            n = 21;
            for (r = 0; r < (int)(sizeof(regs) / sizeof(regs[0])); r++) {
                rl[n++] = ' ';
                hb_hex(rl + n, (unsigned long long)st.__x[regs[r]]);
                n += 16;
            }
            rl[n++] = '\n';
            if (write(2, rl, n) < 0) {
            }
            /* inside SpuHeap_FindBlock .. SpuHeap_Alloc (they follow each other; the addresses move with every build) */
            if (pc >= (unsigned long long)(uintptr_t)&SpuHeap_FindBlock - base &&
                pc < (unsigned long long)(uintptr_t)&SpuHeap_FindBlock - base + 0x600ULL) {
                char tl[1100];
                const unsigned *t = gSpuHeapBlocks; /* 17 x {state, addr, size} */
                memcpy(tl, "bt3 spu ", 8);
                n = 8;
                for (w = 0; w < 17 * 3; w++) {
                    char h[16];
                    tl[n++] = (w % 3 == 0) ? '|' : ' ';
                    hb_hex(h, (unsigned long long)t[w]);
                    memcpy(tl + n, h + 8, 8); /* a 32-bit word: the low 8 of the 16 digits */
                    n += 8;
                }
                tl[n++] = '\n';
                if (write(2, tl, n) < 0) {
                }
                if (gSndMgr != 0) { /* the 8 sound bank slots: {spuAddr, bodySize, hdSize, seqSize, spuSize, pending, body, hd} */
                    char bl[8 * 8 * 9 + 32];
                    const unsigned char *m = (const unsigned char *)(base + gSndMgr);
                    int bk, f;
                    static const int fld[] = {0x00, 0x18, 0x1C, 0x20, 0x24, 0x30, 0x0C, 0x10};
                    memcpy(bl, "bt3 snd ", 8);
                    n = 8;
                    for (bk = 0; bk < 8; bk++) {
                        bl[n++] = '|';
                        for (f = 0; f < 8; f++) {
                            hb_hex32(bl + n, *(const unsigned *)(m + bk * 0x4C + fld[f]));
                            n += 8;
                            bl[n++] = ' ';
                        }
                    }
                    bl[n++] = '\n';
                    if (write(2, bl, n) < 0) {
                    }
                }
            }
        }
    }
    return NULL;
}

static void start_heartbeat(void) {
    pthread_t th;
    sGameThread = mach_thread_self();
    if (pthread_create(&th, NULL, heartbeat, NULL) == 0) {
        pthread_detach(th);
    }
}

static int game_entry(int argc, char *argv[]) {
    fprintf(stderr, "bt3 iOS: SDL started the app, calling the game\n");
    /* A release program (gPortDataStripped == 1, set by the packaging step) turns the GS renderer on by itself;
       this build is not post-processed, so without BT3_GS the game ran with no window and no picture (bt3 logs
       52-57: no "bt3: display" / GPU line, black screen). The default back end is Vulkan (MoltenVK). */
    if (getenv("BT3_GS") == NULL) {
        setenv("BT3_GS", "gpu", 1);
    }
    if (getenv("BT3_GS_VERBOSE") == NULL) {
        setenv("BT3_GS_VERBOSE", "1", 1); /* bring-up: draws, vertices and never-uploaded textures per frame in the log */
    }
    if (getenv("BT3_GS_DUMP") == NULL) {
        setenv("BT3_GS_DUMP", "5300", 1); /* bring-up: list the primitives, registers and uploads of frame 5300 (the start screen, after the intro movie) */
    }
    fprintf(stderr, "bt3 iOS: BT3_GS=%s\n", getenv("BT3_GS"));
    SDL_AddEventWatch(life_watch, NULL);
    start_heartbeat();
    ios_prepare_data(); /* no gamedata yet: unpack the .iso the user copied into Documents (shows its progress) */
    find_moltenvk();
    return __wrap_main(argc, argv);
}

int main(int argc, char **argv) {
    fprintf(stderr, "bt3 iOS: main, starting SDL\n");
    return SDL_RunApp(argc, argv, game_entry, NULL);
}
#endif
