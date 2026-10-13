/* Logic test of port/tools/p32rebase.py on x86-64 Linux: 4-byte pointers as offsets from bt3_hdr. */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <sys/mman.h>
extern char bt3_hdr[];
struct N { int v; uint32_t next; };
extern struct N n1, n2;
extern uint32_t gHead;
unsigned sum(unsigned p); void setv(unsigned p, int v); unsigned head(void); unsigned addr(void);
void fill(unsigned p); unsigned bump(unsigned p); int callf(unsigned f, int x); unsigned fnptr(void);
int cmpx(unsigned p); void initfn(void);
#define OFF(p) ((unsigned)((char *)(p) - bt3_hdr))
#define PTR(o) ((void *)(bt3_hdr + (o)))
static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); fails++; } } while (0)
int main(void) {
    CHECK(n1.next == OFF(&n2));                       /* static data: .long n2 - hdr */
    CHECK(gHead == OFF(&n1));
    CHECK(head() == OFF(&n1));                        /* load of a 4-byte pointer */
    CHECK(addr() == OFF(&n2));                        /* constant address in code */
    CHECK(sum(head()) == 3);                          /* loads through 4-byte pointers, chained */
    setv(OFF(&n2), 40); CHECK(n2.v == 40 && sum(head()) == 41);          /* store */
    /* an arena far above the header (> 4 GB away from address 0) */
    char *arena = mmap(bt3_hdr + 0x30000000, 1 << 20, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    CHECK(arena != MAP_FAILED && (uintptr_t)arena >= (uintptr_t)bt3_hdr && (uintptr_t)arena - (uintptr_t)bt3_hdr < 0xffffffffu);
    fill(OFF(arena)); CHECK(arena[0] == 7 && arena[15] == 7 && arena[16] == 0);   /* memset through a 4-byte pointer */
    struct N *a = (struct N *)arena, *b = (struct N *)(arena + 64);
    a->v = 5; a->next = OFF(b); b->v = 6; b->next = 0;
    CHECK(sum(OFF(a)) == 11);
    CHECK(bump(OFF(a)) == 5 && a->v == 6);            /* atomicrmw */
    CHECK(cmpx(OFF(a)) == 1 && a->v == 99);           /* cmpxchg */
    initfn(); CHECK(callf(fnptr(), 21) == 42);                  /* call through a 4-byte function pointer */
    printf(fails ? "P32 TEST FAILED\n" : "P32 TEST OK\n");
    return fails != 0;
}
