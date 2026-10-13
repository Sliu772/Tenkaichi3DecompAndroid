// Low-memory probe for the iOS port: can this process get memory BELOW 4 GB (needed by the ptr32 model)?
// Built twice by CI: default __PAGEZERO (4 GB) and -pagezero_size 0x4000. Shows the result on screen and writes probe.txt.
#import <UIKit/UIKit.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>
#include <sys/mman.h>
#include <mach/mach.h>
#include <mach-o/dyld.h>
#include <mach-o/getsect.h>
#include <mach-o/ldsyms.h>

#define LIM 0x100000000ULL
static char out[16384];
static void P(const char *f, ...) {
    va_list a; va_start(a, f);
    size_t n = strlen(out);
    vsnprintf(out + n, sizeof(out) - n, f, a);
    va_end(a);
}

static void run(void) {
    out[0] = 0;
    const struct segment_command_64 *pz = (const struct segment_command_64 *)getsegbyname("__PAGEZERO");
    P("__PAGEZERO size: 0x%llx\n", pz ? (unsigned long long)pz->vmsize : 0ULL);
    P("main image header: %p (slide 0x%lx)\n", (void *)&_mh_execute_header, (long)_dyld_get_image_vmaddr_slide(0));
    int local = 0; void *h = malloc(64);
    P("stack %p  malloc %p\n", (void *)&local, h);
    P("page size: %lu\n\n", (unsigned long)vm_page_size);

    static const uint64_t sizes[] = { 1ULL << 30, 512ULL << 20, 256ULL << 20, 64ULL << 20 };
    for (int s = 0; s < 4; s++) {
        uint64_t sz = sizes[s]; int ok = 0; void *first = NULL; uint64_t hint = 0;
        for (uint64_t a = 0x04000000ULL; a + sz <= LIM && !ok; a += 0x04000000ULL) {
            void *p = mmap((void *)a, sz, PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE, -1, 0);
            if (p == MAP_FAILED) continue;
            if ((uint64_t)(uintptr_t)p + sz <= LIM) { ok = 1; first = p; hint = a; ((volatile char *)p)[0] = 1; ((volatile char *)p)[sz - 1] = 1; }
            munmap(p, sz);
        }
        if (ok) P("mmap %4llu MB below 4GB: YES at %p (hint 0x%llx)\n", (unsigned long long)(sz >> 20), first, (unsigned long long)hint);
        else    P("mmap %4llu MB below 4GB: NO\n", (unsigned long long)(sz >> 20));
    }

    // fixed address (what a ptr32 heap would do): vm_allocate / mmap MAP_FIXED at 0x10000000
    vm_address_t va = 0x10000000; kern_return_t kr = vm_allocate(mach_task_self(), &va, 256ULL << 20, 0);
    P("\nvm_allocate fixed 0x10000000 x256MB: %s (kr=%d, addr 0x%lx)\n", kr == KERN_SUCCESS ? "YES" : "NO", kr, (unsigned long)va);
    if (kr == KERN_SUCCESS) { ((volatile char *)va)[0] = 1; vm_deallocate(mach_task_self(), va, 256ULL << 20); }
    void *f = mmap((void *)0x10000000, 256ULL << 20, PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE | MAP_FIXED, -1, 0);
    P("mmap MAP_FIXED 0x10000000 x256MB: %s (%p, errno %d)\n", f == MAP_FAILED ? "NO" : "YES", f, f == MAP_FAILED ? errno : 0);
    if (f != MAP_FAILED) munmap(f, 256ULL << 20);

    // largest total we can hold below 4GB (64MB chunks, kept)
    int n = 0; static void *keep[64];
    for (uint64_t a = 0x04000000ULL; a + (64ULL << 20) <= LIM && n < 64; a += 0x04000000ULL) {
        void *p = mmap((void *)a, 64ULL << 20, PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE, -1, 0);
        if (p == MAP_FAILED) continue;
        if ((uint64_t)(uintptr_t)p + (64ULL << 20) <= LIM) keep[n++] = p; else munmap(p, 64ULL << 20);
    }
    P("total held below 4GB in 64MB chunks: %d MB\n", n * 64);
    for (int i = 0; i < n; i++) munmap(keep[i], 64ULL << 20);
    free(h);

    NSString *path = [NSSearchPathForDirectoriesInDomains(NSDocumentDirectory, NSUserDomainMask, YES).firstObject stringByAppendingPathComponent:@"probe.txt"];
    [[NSString stringWithUTF8String:out] writeToFile:path atomically:YES encoding:NSUTF8StringEncoding error:nil];
}

@interface VC : UIViewController @end
@implementation VC
- (void)viewDidLoad {
    [super viewDidLoad];
    self.view.backgroundColor = UIColor.blackColor;
    UITextView *t = [[UITextView alloc] initWithFrame:CGRectInset(self.view.bounds, 8, 40)];
    t.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
    t.backgroundColor = UIColor.blackColor; t.textColor = UIColor.greenColor; t.editable = NO;
    t.font = [UIFont monospacedSystemFontOfSize:12 weight:UIFontWeightRegular];
    run(); t.text = [NSString stringWithUTF8String:out];
    [self.view addSubview:t];
}
@end
@interface AD : UIResponder <UIApplicationDelegate> @property UIWindow *window; @end
@implementation AD
- (BOOL)application:(UIApplication *)a didFinishLaunchingWithOptions:(NSDictionary *)o {
    self.window = [[UIWindow alloc] initWithFrame:UIScreen.mainScreen.bounds];
    self.window.rootViewController = [VC new]; [self.window makeKeyAndVisible]; return YES;
}
@end
int main(int argc, char *argv[]) { @autoreleasepool { return UIApplicationMain(argc, argv, nil, NSStringFromClass([AD class])); } }
