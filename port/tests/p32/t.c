struct N { int v; struct N * __ptr32 __uptr next; };
extern struct N n2;
struct N n1 = {1, &n2};
struct N n2 = {2, 0};
struct N * __ptr32 __uptr gHead = &n1;
int dbl(int x) { return 2 * x; }
int (* __ptr32 __uptr gFn)(int);
void initfn(void) { gFn = dbl; }
int sum(struct N * __ptr32 __uptr p) { int s = 0; while (p) { s += p->v; p = p->next; } return s; }
void setv(struct N * __ptr32 __uptr p, int v) { p->v = v; }
struct N * __ptr32 __uptr head(void) { return gHead; }
struct N * __ptr32 __uptr addr(void) { return &n2; }
void fill(char * __ptr32 __uptr p) { __builtin_memset(p, 7, 16); }
int bump(struct N * __ptr32 __uptr p) { return __atomic_fetch_add(&p->v, 1, __ATOMIC_SEQ_CST); }
int cmpx(struct N * __ptr32 __uptr p) { int e = 6; return __atomic_compare_exchange_n(&p->v, &e, 99, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST); }
int callf(int (* __ptr32 __uptr f)(int), int x) { return f(x); }
int (* __ptr32 __uptr fnptr(void))(int) { return gFn; }
