/* Freestanding C with what compilers emit for static and position-
   independent code: globals through the GOT, local data, string constants,
   jump tables, function pointers, initializer tables, and thread-local
   storage. main returns 0, or the number of the first check that fails. */
typedef unsigned long u64;

extern int external_value;
int external_value = 40;
static int counter = 1;
static const char message[] = "linked";
__thread int tls_value = 7;
static __thread int tls_local = 9;

static int add(int a, int b) { return a + b; }
static int sub(int a, int b) { return a - b; }
static int mul(int a, int b) { return a * b; }
int (*const operations[])(int, int) = {add, sub, mul};
int (*selected)(int, int) = add;

static int initialized;
static void initialize(void) { initialized = 5; }
__attribute__((section(".init_array"), used)) static void (*init_entry)(void) = initialize;

__attribute__((noinline)) int classify(int value) {
    switch (value) {
    case 0: return 11;
    case 1: return 22;
    case 2: return 33;
    case 3: return 44;
    case 4: return 55;
    case 5: return 66;
    default: return -1;
    }
}

int main(void) {
    if (external_value + counter != 41) return 1;
    if (message[0] != 'l' || message[5] != 'd') return 2;
    if (classify(3) != 44 || classify(5) != 66 || classify(9) != -1) return 3;
    if (operations[1](9, 4) != 5 || operations[2](6, 7) != 42) return 4;
    if (selected(2, 3) != 5) return 5;
    if (tls_value + tls_local != 16) return 6;
    tls_value = 1;
    if (tls_value != 1) return 7;
    if (initialized != 5) return 8;
    return 0;
}
