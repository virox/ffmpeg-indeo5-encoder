/*
 * iv5ref - run Intel's original IR50_32.DLL Indeo 5 decoder natively on
 * Linux (i386, freestanding, no libc / Wine) to verify encoder output.
 *
 * usage: iv5ref [-v] IR50_32.DLL frames.bin width height out.raw [format] [mask.raw] [key=N] [mmx=0|1]
 *   frames.bin : sequence of [u32 little-endian size][IV50 frame data]
 *   out.raw    : decoded frames, YVU9 planar (default) or bottom-up RGB24
 *
 * Only the Win32 functions the codec actually touches are implemented;
 * anything else aborts with the function name.
 *
 * Build: gcc -m32 -O1 -ffreestanding -fno-pic -fno-stack-protector
 *            -fno-builtin -nostdlib -static -o iv5ref iv5ref.c
 */

typedef unsigned int   u32;
typedef int            i32;
typedef unsigned short u16;
typedef unsigned char  u8;
typedef unsigned int   size_t;

#define WINAPI __attribute__((stdcall))

/* ------------------------------------------------------------------ */
/* syscalls & tiny libc                                                */

static int sys3(int n, int a, int b, int c)
{
    int r;
    __asm__ volatile ("int $0x80" : "=a"(r) : "a"(n), "b"(a), "c"(b), "d"(c) : "memory");
    return r;
}

static void sys_exit(int c) { sys3(1, c, 0, 0); for (;;); }
static int  sys_write(int fd, const void *b, int n) { return sys3(4, fd, (int)b, n); }
static int  sys_read(int fd, void *b, int n) { return sys3(3, fd, (int)b, n); }
static int  sys_open(const char *p, int f, int m) { return sys3(5, (int)p, f, m); }
static int  sys_close(int fd) { return sys3(6, fd, 0, 0); }

static void *sys_mmap(void *addr, size_t len, int prot, int flags)
{
    /* old_mmap: arguments passed in a block */
    u32 args[6] = { (u32)addr, len, prot, flags, (u32)-1, 0 };
    return (void *)sys3(90, (int)args, 0, 0);
}

void *memcpy(void *d, const void *s, size_t n)
{
    u8 *dd = d; const u8 *ss = s;
    while (n--) *dd++ = *ss++;
    return d;
}

void *memmove(void *d, const void *s, size_t n)
{
    u8 *dd = d; const u8 *ss = s;
    if (dd < ss) while (n--) *dd++ = *ss++;
    else { dd += n; ss += n; while (n--) *--dd = *--ss; }
    return d;
}

void *memset(void *d, int c, size_t n)
{
    u8 *dd = d;
    while (n--) *dd++ = c;
    return d;
}

int memcmp(const void *a, const void *b, size_t n)
{
    const u8 *x = a, *y = b;
    for (; n; n--, x++, y++) if (*x != *y) return *x - *y;
    return 0;
}

static size_t my_strlen(const char *s) { size_t n = 0; while (s[n]) n++; return n; }

static int my_stricmp(const char *a, const char *b)
{
    for (;; a++, b++) {
        int x = *a, y = *b;
        if (x >= 'A' && x <= 'Z') x += 32;
        if (y >= 'A' && y <= 'Z') y += 32;
        if (x != y || !x) return x - y;
    }
}

static void puts_fd(int fd, const char *s) { sys_write(fd, s, my_strlen(s)); }

static void put_hex(int fd, u32 v)
{
    char b[11] = "0x";
    for (int i = 0; i < 8; i++) b[2 + i] = "0123456789abcdef"[(v >> (28 - 4 * i)) & 15];
    b[10] = 0; puts_fd(fd, b);
}

static void put_dec(int fd, i32 v)
{
    char b[16]; int i = 15; u32 u = v < 0 ? -v : v;
    b[i] = 0;
    do { b[--i] = '0' + u % 10; u /= 10; } while (u);
    if (v < 0) b[--i] = '-';
    puts_fd(fd, b + i);
}

static void die(const char *m) { puts_fd(2, "iv5ref: "); puts_fd(2, m); puts_fd(2, "\n"); sys_exit(1); }

static int verbose;
static int clock_step;       /* 1: clocks advance on every call (codec may drop bands) */
static u32 qpc_t, tgt_t;
static void trace(const char *fn, u32 a)
{
    if (!verbose) return;
    puts_fd(2, "  [api] "); puts_fd(2, fn); puts_fd(2, " "); put_hex(2, a); puts_fd(2, "\n");
}

/* simple heap: bump allocator with size header, free is a no-op */
static u8 *heap_cur, *heap_end;
static void *xmalloc(size_t n)
{
    u8 *p;
    n = (n + 15) & ~15u;
    if (!heap_cur || heap_cur + n + 16 > heap_end) {
        size_t chunk = n + 16 > (64u << 20) ? n + 16 : (64u << 20);
        heap_cur = sys_mmap(0, chunk, 7, 0x22);
        if ((u32)heap_cur > 0xfffff000u) die("out of memory");
        heap_end = heap_cur + chunk;
    }
    p = heap_cur; heap_cur += n + 16;
    *(u32 *)p = n;
    memset(p + 16, 0, n);
    return p + 16;
}
static size_t xsize(void *p) { return p ? *(u32 *)((u8 *)p - 16) : 0; }
static void *xrealloc(void *p, size_t n)
{
    void *q = xmalloc(n);
    if (p) memcpy(q, p, xsize(p) < n ? xsize(p) : n);
    return q;
}

/* ------------------------------------------------------------------ */
/* Win32 API subset                                                    */

static u32 last_error;
static u32 dll_base;
static u32 tls_slots[64];
static int tls_used;

/* kernel32 */
static WINAPI u32 k_GetVersion(void) { trace("GetVersion", 0); return 0x0A280105; /* XP-ish, NT */ }
static WINAPI u32 k_GetVersionExA(u32 *vi)
{
    trace("GetVersionExA", 0);
    vi[1] = 5; vi[2] = 1; vi[3] = 2600; vi[4] = 2; /* VER_PLATFORM_WIN32_NT */
    ((char *)&vi[5])[0] = 0;
    return 1;
}
static WINAPI char *k_GetCommandLineA(void) { return "iv5ref"; }
static WINAPI void k_GetStartupInfoA(u32 *si) { memset(si, 0, 68); si[0] = 68; }
static WINAPI u32 k_GetStdHandle(u32 n) { return 0x100 + (n & 0xff); }
static WINAPI u32 k_GetFileType(u32 h) { (void)h; return 2; /* FILE_TYPE_CHAR */ }
static WINAPI u32 k_SetHandleCount(u32 n) { return n; }
static WINAPI char *k_GetEnvironmentStrings(void) { static char e[4] = { 0, 0 }; return e; }
static WINAPI u16 *k_GetEnvironmentStringsW(void) { static u16 e[2] = { 0, 0 }; return e; }
static WINAPI u32 k_FreeEnvironmentStrings(void *p) { (void)p; return 1; }
static WINAPI u32 k_GetModuleFileNameA(u32 h, char *b, u32 n)
{
    const char *s = h ? "C:\\WINDOWS\\SYSTEM32\\IR50_32.DLL" : "C:\\iv5ref.exe";
    u32 l = my_strlen(s);
    trace("GetModuleFileNameA", h);
    if (l >= n) l = n - 1;
    memcpy(b, s, l); b[l] = 0;
    return l;
}
static WINAPI u32 k_GetACP(void) { return 1252; }
static WINAPI u32 k_GetOEMCP(void) { return 437; }
static WINAPI u32 k_GetCPInfo(u32 cp, u8 *ci)
{
    (void)cp; memset(ci, 0, 20); ((u32 *)ci)[0] = 1; ci[4] = '?'; return 1;
}
static WINAPI u32 k_HeapCreate(u32 a, u32 b, u32 c) { (void)a; (void)b; (void)c; return 0x4ea9; }
static WINAPI u32 k_HeapDestroy(u32 h) { (void)h; return 1; }
static WINAPI void *k_HeapAlloc(u32 h, u32 f, u32 n) { (void)h; (void)f; return xmalloc(n); }
static WINAPI u32 k_HeapFree(u32 h, u32 f, void *p) { (void)h; (void)f; (void)p; return 1; }
static WINAPI void *k_VirtualAlloc(void *a, u32 n, u32 type, u32 prot)
{
    (void)prot;
    trace("VirtualAlloc", n);
    if (a && !(type & 0x2000)) return a;  /* commit inside an earlier reservation */
    return xmalloc(n);
}
static WINAPI u32 k_VirtualFree(void *a, u32 n, u32 t) { (void)a; (void)n; (void)t; return 1; }
static WINAPI u32 k_TlsAlloc(void) { return tls_used < 64 ? tls_used++ : 0xffffffff; }
static WINAPI u32 k_TlsSetValue(u32 i, u32 v) { if (i >= 64) return 0; tls_slots[i] = v; return 1; }
static WINAPI u32 k_TlsGetValue(u32 i) { last_error = 0; return i < 64 ? tls_slots[i] : 0; }
static WINAPI u32 k_TlsFree(u32 i) { (void)i; return 1; }
static WINAPI u32 k_GetCurrentThreadId(void) { return 1234; }
static WINAPI u32 k_GetCurrentProcess(void) { return 0xffffffff; }
static WINAPI void k_CritSec(void *cs) { (void)cs; }
static WINAPI i32 k_InterlockedIncrement(i32 *p) { return ++*p; }
static WINAPI i32 k_InterlockedDecrement(i32 *p) { return --*p; }
static WINAPI u32 k_DisableThreadLibraryCalls(u32 h) { (void)h; return 1; }
static WINAPI u32 k_GetModuleHandleA(const char *n)
{
    trace("GetModuleHandleA", 0);
    if (verbose && n) { puts_fd(2, "     name="); puts_fd(2, n); puts_fd(2, "\n"); }
    if (!n) return 0x400000;
    if (!my_stricmp(n, "IR50_32.DLL") || !my_stricmp(n, "IR50_32")) return dll_base;
    return 0;
}
/* LoadLibraryA: only Intel's helper modules (ir50_qc*.dll) are loaded, from
 * the directory of the main DLL; everything else fails */
static char dll_dir[512];
static u32 load_pe(const char *path);
static u32 pe_export(u32 img, const char *name);
static struct { char name[32]; u32 base; } modules[8];
static int nmodules;
typedef WINAPI i32 (*DllMainFn2)(u32 h, u32 reason, u32 res);
static WINAPI u32 k_LoadLibraryA(const char *n)
{
    char path[600];
    const char *b = n;
    for (const char *q = n; *q; q++) if (*q == '\\' || *q == '/') b = q + 1;
    if (verbose) { puts_fd(2, "  [api] LoadLibraryA "); puts_fd(2, n); puts_fd(2, "\n"); }
    for (int i = 0; i < nmodules; i++)
        if (!my_stricmp(modules[i].name, b)) return modules[i].base;
    if ((b[0] | 32) == 'i' && (b[1] | 32) == 'r' && b[2] == '5' && b[3] == '0' && b[4] == '_' && nmodules < 8) {
        u32 l = my_strlen(dll_dir), save = dll_base, base, pe;
        memcpy(path, dll_dir, l);
        for (u32 i = 0; b[i] && l < 590; i++) {
            char c = b[i]; if (c >= 'a' && c <= 'z') c -= 32;   /* files are upper case */
            path[l++] = c;
        }
        path[l] = 0;
        if (sys_open(path, 0, 0) < 0) { last_error = 126; return 0; }
        base = load_pe(path);
        dll_base = save;
        pe = *(u32 *)(base + 0x3c);
        ((DllMainFn2)(base + *(u32 *)(base + pe + 24 + 16)))(base, 1, 0);
        memcpy(modules[nmodules].name, b, my_strlen(b) < 31 ? my_strlen(b) + 1 : 31);
        modules[nmodules].base = base;
        nmodules++;
        if (verbose) { puts_fd(2, "     loaded at "); put_hex(2, base); puts_fd(2, "\n"); }
        return base;
    }
    last_error = 126;
    return 0;
}
static WINAPI u32 k_FreeLibrary(u32 h) { (void)h; return 1; }
static WINAPI void *k_GetProcAddress(u32 h, const char *n)
{
    if (verbose) { puts_fd(2, "  [api] GetProcAddress "); puts_fd(2, (u32)n > 0xffff ? n : "#ord"); puts_fd(2, "\n"); }
    for (int i = 0; i < nmodules; i++)
        if (modules[i].base == h && (u32)n > 0xffff) {
            u32 f = pe_export(h, n);
            if (f) return (void *)f;
        }
    last_error = 127;
    return 0;
}
/* Global/Local memory: handles are the pointers themselves */
static WINAPI void *k_GlobalAlloc(u32 f, u32 n) { (void)f; return xmalloc(n ? n : 1); }
static WINAPI void *k_GlobalReAlloc(void *p, u32 n, u32 f) { (void)f; return xrealloc(p, n); }
static WINAPI void *k_Identity(void *p) { return p; }
static WINAPI u32 k_GlobalUnlock(void *p) { (void)p; return 1; }
static WINAPI u32 k_FreeNull(void *p) { (void)p; return 0; }
static WINAPI void *k_LocalAlloc(u32 f, u32 n) { (void)f; return xmalloc(n ? n : 1); }

/* named shared memory: emulated with plain allocations */
struct mapping { char name[64]; u32 size; void *mem; };
static struct mapping maps[16];
static int nmaps;
static WINAPI u32 k_CreateFileMappingA(u32 f, void *sa, u32 prot, u32 hi, u32 lo, const char *name)
{
    struct mapping *m;
    (void)f; (void)sa; (void)prot; (void)hi;
    if (verbose) { puts_fd(2, "  [api] CreateFileMappingA "); puts_fd(2, name ? name : "(anon)"); puts_fd(2, " "); put_hex(2, lo); puts_fd(2, "\n"); }
    for (int i = 0; name && i < nmaps; i++)
        if (!my_stricmp(maps[i].name, name)) { last_error = 183; return 0x1000 + i; }
    if (nmaps >= 16) return 0;
    m = &maps[nmaps];
    if (name) memcpy(m->name, name, my_strlen(name) < 63 ? my_strlen(name) + 1 : 63);
    m->size = lo; m->mem = xmalloc(lo);
    last_error = 0;
    return 0x1000 + nmaps++;
}
static WINAPI u32 k_OpenFileMappingA(u32 acc, u32 inh, const char *name)
{
    (void)acc; (void)inh;
    if (verbose) { puts_fd(2, "  [api] OpenFileMappingA "); puts_fd(2, name); puts_fd(2, "\n"); }
    for (int i = 0; i < nmaps; i++)
        if (!my_stricmp(maps[i].name, name)) return 0x1000 + i;
    last_error = 2;
    return 0;
}
static WINAPI void *k_MapViewOfFile(u32 h, u32 acc, u32 hi, u32 lo, u32 n)
{
    (void)acc; (void)hi; (void)n;
    if (h < 0x1000 || h >= 0x1000u + nmaps) return 0;
    return (u8 *)maps[h - 0x1000].mem + lo;
}
static WINAPI u32 k_True1(u32 a) { (void)a; return 1; }
static WINAPI u32 k_CreateSemaphoreA(void *sa, i32 init, i32 max, const char *n) { (void)sa; (void)init; (void)max; (void)n; return 0x2000; }
static WINAPI u32 k_WaitForSingleObject(u32 h, u32 t) { (void)h; (void)t; return 0; }
static WINAPI u32 k_ReleaseSemaphore(u32 h, i32 c, i32 *prev) { (void)h; (void)c; if (prev) *prev = 0; return 1; }
static WINAPI void k_OutputDebugStringA(const char *s) { if (verbose) { puts_fd(2, "  [dbg] "); puts_fd(2, s); } }
static WINAPI void k_Sleep(u32 ms) { (void)ms; }
static WINAPI u32 k_GetLastError(void) { return last_error; }
static WINAPI void k_SetLastError(u32 e) { last_error = e; }
static WINAPI void k_GetSystemInfo(u32 *si)
{
    memset(si, 0, 36);
    si[1] = 4096; si[2] = 0x10000; si[3] = 0x7ffeffff; si[4] = 1; si[5] = 1;
    si[6] = 586; si[7] = 0x10000; ((u16 *)si)[16] = 6;
}
static WINAPI u32 k_GetSystemDirectoryA(char *b, u32 n)
{
    const char *s = "C:\\WINDOWS\\SYSTEM32"; u32 l = my_strlen(s);
    if (n <= l) return l + 1;
    memcpy(b, s, l + 1); return l;
}
static WINAPI u32 k_GetShortPathNameA(const char *a, char *b, u32 n)
{
    u32 l = my_strlen(a); if (n <= l) return l + 1; memcpy(b, a, l + 1); return l;
}
static WINAPI i32 k_lstrlenA(const char *s) { return s ? my_strlen(s) : 0; }
static WINAPI u32 k_WritePrivateProfileStringA(void *a, void *b, void *c, void *d) { (void)a; (void)b; (void)c; (void)d; return 1; }
static WINAPI u32 k_IsBad2(void *p, u32 n) { (void)n; return !p; }
static WINAPI u32 k_IsBadCodePtr(void *p) { return !p; }
static WINAPI i32 k_MultiByteToWideChar(u32 cp, u32 fl, const char *s, i32 n, u16 *d, i32 dn)
{
    (void)cp; (void)fl;
    if (n < 0) n = my_strlen(s) + 1;
    if (!dn) return n;
    for (i32 i = 0; i < n && i < dn; i++) d[i] = (u8)s[i];
    return n < dn ? n : dn;
}
static WINAPI i32 k_WideCharToMultiByte(u32 cp, u32 fl, const u16 *s, i32 n, char *d, i32 dn, void *dc, void *ud)
{
    (void)cp; (void)fl; (void)dc; (void)ud;
    if (n < 0) { n = 0; while (s[n]) n++; n++; }
    if (!dn) return n;
    for (i32 i = 0; i < n && i < dn; i++) d[i] = (char)s[i];
    return n < dn ? n : dn;
}
static WINAPI u32 k_Fail4(u32 a, u32 b, u32 c, u32 d) { (void)a; (void)b; (void)c; (void)d; return 0; }
static WINAPI u32 k_Fail5(u32 a, u32 b, u32 c, u32 d, u32 e) { (void)a; (void)b; (void)c; (void)d; (void)e; return 0; }
static WINAPI u32 k_Fail6(u32 a, u32 b, u32 c, u32 d, u32 e, u32 f) { (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; return 0; }
static WINAPI u32 k_SetUnhandledExceptionFilter(u32 f) { (void)f; return 0; }
static WINAPI void k_ExitProcess(u32 c) { die("ExitProcess called"); (void)c; }
static WINAPI u32 k_TerminateProcess(u32 h, u32 c) { (void)h; (void)c; die("TerminateProcess called"); return 0; }
static WINAPI u32 k_WriteFile(u32 h, const void *b, u32 n, u32 *w, void *o)
{
    (void)h; (void)o; sys_write(2, b, n); if (w) *w = n; return 1;
}
static WINAPI u32 k_SetFilePointer(u32 h, i32 lo, i32 *hi, u32 m) { (void)h; (void)lo; (void)hi; (void)m; return 0xffffffff; }
static WINAPI u32 k_SetStdHandle(u32 a, u32 b) { (void)a; (void)b; return 1; }
/* real time in microseconds (gettimeofday), so the codec's load
 * estimate (which decides how many wavelet bands to decode) is realistic */
static u32 now_us(void)
{
    u32 tv[2];
    sys3(78, (int)tv, 0, 0);
    return tv[0] * 1000000u + tv[1];
}
static WINAPI u32 k_QueryPerformanceCounter(u32 *c) { c[0] = clock_step ? (qpc_t += 1000) : now_us(); c[1] = 0; return 1; }
static WINAPI u32 k_QueryPerformanceFrequency(u32 *c) { c[0] = 1000000; c[1] = 0; return 1; }
static WINAPI u32 k_timeGetTime(void) { return clock_step ? (tgt_t += 10) : now_us() / 1000; }
static WINAPI void k_RaiseException(u32 a, u32 b, u32 c, void *d) { (void)b; (void)c; (void)d; put_hex(2, a); die(" RaiseException"); }
static WINAPI void k_RtlUnwind(void *a, void *b, void *c, u32 d) { (void)a; (void)b; (void)c; (void)d; die("RtlUnwind"); }
static int atoi_res(const char *s) { int v = 0; while (*s >= '0' && *s <= '9') v = v * 10 + *s++ - '0'; return v; }
/* resources: walk the .rsrc tree type -> name -> first language */
static u32 rsrc_dir_find(u8 *root, u8 *dir, const char *key)
{
    u16 nnamed = *(u16 *)(dir + 12), nid = *(u16 *)(dir + 14);
    u32 *e = (u32 *)(dir + 16);
    for (u32 i = 0; i < (u32)nnamed + nid; i++, e += 2) {
        int match;
        if ((u32)key <= 0xffff) {
            match = !(e[0] & 0x80000000u) && e[0] == (u32)key;
        } else if (key[0] == '#') {
            match = !(e[0] & 0x80000000u) && e[0] == (u32)atoi_res(key + 1);
        } else {
            u16 *ws = (u16 *)(root + (e[0] & 0x7fffffff));
            u32 l = ws[0], k;
            match = (e[0] & 0x80000000u) && l == my_strlen(key);
            for (k = 0; match && k < l; k++) {
                int c = (u8)key[k]; if (c >= 'a' && c <= 'z') c -= 32;
                int d = ws[1 + k];  if (d >= 'a' && d <= 'z') d -= 32;
                match = c == d;
            }
        }
        if (match) return e[1];
    }
    return 0xffffffff;
}
static WINAPI u32 k_FindResourceA(u32 h, const char *n, const char *t)
{
    u32 pe = *(u32 *)(dll_base + 0x3c);
    u8 *root = (u8 *)dll_base + *(u32 *)(dll_base + pe + 24 + 96 + 2 * 8);
    u32 off;
    (void)h;
    if (verbose) {
        puts_fd(2, "  [api] FindResourceA name=");
        if ((u32)n > 0xffff) puts_fd(2, n); else put_hex(2, (u32)n);
        puts_fd(2, " type=");
        if ((u32)t > 0xffff) puts_fd(2, t); else put_hex(2, (u32)t);
        puts_fd(2, "\n");
    }
    off = rsrc_dir_find(root, root, t);
    if (off == 0xffffffff || !(off & 0x80000000u)) return 0;
    off = rsrc_dir_find(root, root + (off & 0x7fffffff), n);
    if (off == 0xffffffff || !(off & 0x80000000u)) return 0;
    /* first language entry */
    off = ((u32 *)(root + (off & 0x7fffffff) + 16))[1];
    if (off & 0x80000000u) return 0;
    return (u32)(root + off);   /* IMAGE_RESOURCE_DATA_ENTRY */
}
static WINAPI void *k_LoadResource(u32 h, u32 *de) { (void)h; return de ? (void *)(dll_base + de[0]) : 0; }
static WINAPI i32 k_DefDriverProc(u32 id, u32 h, u32 msg, u32 l1, u32 l2) { (void)id; (void)h; (void)l1; (void)l2; trace("DefDriverProc", msg); return 0; }
/* registry: nothing exists; key creation "succeeds" with a dummy handle */
static WINAPI u32 r_Open5(u32 k, const char *s, u32 o, u32 a, u32 *res)
{
    (void)k; (void)o; (void)a;
    if (verbose) { puts_fd(2, "  [api] RegOpenKeyExA "); puts_fd(2, s ? s : ""); puts_fd(2, "\n"); }
    if (res) *res = 0;
    return 2;
}
static WINAPI u32 r_Open3(u32 k, const char *s, u32 *res) { (void)k; (void)s; if (res) *res = 0; return 2; }
static WINAPI u32 r_Create3(u32 k, const char *s, u32 *res) { (void)k; (void)s; *res = 0x3000; return 0; }
static WINAPI u32 r_Create9(u32 k, const char *s, u32 r, void *c, u32 o, u32 a, void *sa, u32 *res, u32 *disp)
{
    (void)k; (void)s; (void)r; (void)c; (void)o; (void)a; (void)sa;
    *res = 0x3000; if (disp) *disp = 1; return 0;
}
static WINAPI u32 r_Close(u32 k) { (void)k; return 0; }
static WINAPI u32 r_QueryEx(u32 k, const char *n, void *r, u32 *t, void *d, u32 *l)
{
    (void)k; (void)r; (void)t; (void)d; (void)l;
    if (verbose) { puts_fd(2, "  [api] RegQueryValueExA "); puts_fd(2, n ? n : ""); puts_fd(2, "\n"); }
    return 2;
}
static WINAPI u32 r_Query4(u32 k, const char *s, char *v, i32 *l) { (void)k; (void)s; (void)v; (void)l; return 2; }
static WINAPI u32 r_SetEx(u32 k, const char *n, u32 r, u32 t, const void *d, u32 l) { (void)k; (void)n; (void)r; (void)t; (void)d; (void)l; return 0; }
static WINAPI u32 r_Set5(u32 k, const char *s, u32 t, const char *d, u32 l) { (void)k; (void)s; (void)t; (void)d; (void)l; return 0; }
static WINAPI u32 r_Del2(u32 k, const char *s) { (void)k; (void)s; return 0; }
static WINAPI u32 r_Enum8(u32 k, u32 i, char *n, u32 *nl, u32 *r, char *c, u32 *cl, void *ft) { (void)k; (void)i; (void)n; (void)nl; (void)r; (void)c; (void)cl; (void)ft; return 259; }

/* user32 wsprintfA (cdecl, varargs): enough for %s %d %u %x %lu %ld %lx %c with width/0 */
static int my_vsprintf(char *out, const char *f, u32 *ap)
{
    char *o = out;
    for (; *f; f++) {
        int w = 0, zero = 0;
        char tmp[16]; int n = 0; u32 v; int neg = 0, base = 10, up = 0;
        if (*f != '%') { *o++ = *f; continue; }
        f++;
        if (*f == '-') f++;
        if (*f == '0') { zero = 1; f++; }
        while (*f >= '0' && *f <= '9') w = w * 10 + *f++ - '0';
        while (*f == 'l' || *f == 'h') f++;
        switch (*f) {
        case 's': { const char *s = (const char *)*ap++; if (!s) s = "(null)"; while (*s) *o++ = *s++; continue; }
        case 'c': *o++ = (char)*ap++; continue;
        case '%': *o++ = '%'; continue;
        case 'd': case 'i': v = *ap++; if ((i32)v < 0) { neg = 1; v = -v; } break;
        case 'u': v = *ap++; break;
        case 'X': up = 1; /* fallthrough */
        case 'x': v = *ap++; base = 16; break;
        default: *o++ = *f; continue;
        }
        do { tmp[n++] = (up ? "0123456789ABCDEF" : "0123456789abcdef")[v % base]; v /= base; } while (v);
        if (neg) tmp[n++] = '-';
        while (n < w) tmp[n++] = zero ? '0' : ' ';
        while (n) *o++ = tmp[--n];
    }
    *o = 0;
    return o - out;
}
static int u_wsprintfA(char *out, const char *f, ...)
{
    return my_vsprintf(out, f, (u32 *)(&f + 1));
}
static WINAPI int u_wvsprintfA(char *out, const char *f, u32 *ap) { return my_vsprintf(out, f, ap); }
static WINAPI int u_LoadStringA(u32 h, u32 id, char *b, int n) { (void)h; (void)id; if (n) b[0] = 0; return 0; }
static WINAPI i32 u_MessageBoxA(u32 h, const char *t, const char *c, u32 f)
{
    (void)h; (void)f; puts_fd(2, "  [MessageBox] "); puts_fd(2, c ? c : ""); puts_fd(2, ": "); puts_fd(2, t ? t : ""); puts_fd(2, "\n");
    return 1;
}
/* ole32 */
static WINAPI u32 o_CoInitialize(void *p) { (void)p; return 0; }
static WINAPI void o_CoUninitialize(void) { }
static WINAPI u32 o_CoCreateInstance(void *a, void *b, u32 c, void *d, void **e) { (void)a; (void)b; (void)c; (void)d; *e = 0; return 0x80040154; }
static WINAPI void *o_CoTaskMemAlloc(u32 n) { return xmalloc(n); }
static WINAPI void o_CoTaskMemFree(void *p) { (void)p; }

struct api { const char *name; void *fn; };
static const struct api apis[] = {
    { "GetVersion", k_GetVersion }, { "GetVersionExA", k_GetVersionExA },
    { "GetCommandLineA", k_GetCommandLineA }, { "GetStartupInfoA", k_GetStartupInfoA },
    { "GetStdHandle", k_GetStdHandle }, { "GetFileType", k_GetFileType },
    { "SetHandleCount", k_SetHandleCount }, { "GetEnvironmentStrings", k_GetEnvironmentStrings },
    { "GetEnvironmentStringsW", k_GetEnvironmentStringsW },
    { "FreeEnvironmentStringsA", k_FreeEnvironmentStrings }, { "FreeEnvironmentStringsW", k_FreeEnvironmentStrings },
    { "GetModuleFileNameA", k_GetModuleFileNameA }, { "GetACP", k_GetACP }, { "GetOEMCP", k_GetOEMCP },
    { "GetCPInfo", k_GetCPInfo }, { "HeapCreate", k_HeapCreate }, { "HeapDestroy", k_HeapDestroy },
    { "HeapAlloc", k_HeapAlloc }, { "HeapFree", k_HeapFree },
    { "VirtualAlloc", k_VirtualAlloc }, { "VirtualFree", k_VirtualFree },
    { "TlsAlloc", k_TlsAlloc }, { "TlsSetValue", k_TlsSetValue }, { "TlsGetValue", k_TlsGetValue },
    { "TlsFree", k_TlsFree }, { "GetCurrentThreadId", k_GetCurrentThreadId },
    { "GetCurrentProcess", k_GetCurrentProcess },
    { "InitializeCriticalSection", k_CritSec }, { "DeleteCriticalSection", k_CritSec },
    { "EnterCriticalSection", k_CritSec }, { "LeaveCriticalSection", k_CritSec },
    { "InterlockedIncrement", k_InterlockedIncrement }, { "InterlockedDecrement", k_InterlockedDecrement },
    { "DisableThreadLibraryCalls", k_DisableThreadLibraryCalls },
    { "GetModuleHandleA", k_GetModuleHandleA }, { "LoadLibraryA", k_LoadLibraryA },
    { "FreeLibrary", k_FreeLibrary }, { "GetProcAddress", k_GetProcAddress },
    { "GlobalAlloc", k_GlobalAlloc }, { "GlobalReAlloc", k_GlobalReAlloc },
    { "GlobalLock", k_Identity }, { "GlobalHandle", k_Identity }, { "GlobalUnlock", k_GlobalUnlock },
    { "GlobalFree", k_FreeNull }, { "LocalAlloc", k_LocalAlloc }, { "LocalLock", k_Identity },
    { "LocalHandle", k_Identity }, { "LocalUnlock", k_GlobalUnlock }, { "LocalFree", k_FreeNull },
    { "CreateFileMappingA", k_CreateFileMappingA }, { "OpenFileMappingA", k_OpenFileMappingA },
    { "MapViewOfFile", k_MapViewOfFile }, { "UnmapViewOfFile", k_True1 }, { "CloseHandle", k_True1 },
    { "CreateSemaphoreA", k_CreateSemaphoreA }, { "WaitForSingleObject", k_WaitForSingleObject },
    { "ReleaseSemaphore", k_ReleaseSemaphore }, { "OutputDebugStringA", k_OutputDebugStringA },
    { "Sleep", k_Sleep }, { "GetLastError", k_GetLastError }, { "SetLastError", k_SetLastError },
    { "GetSystemInfo", k_GetSystemInfo }, { "GetSystemDirectoryA", k_GetSystemDirectoryA },
    { "GetShortPathNameA", k_GetShortPathNameA }, { "lstrlenA", k_lstrlenA },
    { "WritePrivateProfileStringA", k_WritePrivateProfileStringA },
    { "IsBadReadPtr", k_IsBad2 }, { "IsBadWritePtr", k_IsBad2 }, { "IsBadCodePtr", k_IsBadCodePtr },
    { "MultiByteToWideChar", k_MultiByteToWideChar }, { "WideCharToMultiByte", k_WideCharToMultiByte },
    { "LCMapStringA", k_Fail6 }, { "LCMapStringW", k_Fail6 },
    { "GetStringTypeA", k_Fail5 }, { "GetStringTypeW", k_Fail4 },
    { "GetLocaleInfoA", k_Fail4 }, { "GetLocaleInfoW", k_Fail4 },
    { "SetUnhandledExceptionFilter", k_SetUnhandledExceptionFilter },
    { "ExitProcess", k_ExitProcess }, { "TerminateProcess", k_TerminateProcess },
    { "WriteFile", k_WriteFile }, { "FlushFileBuffers", k_True1 }, { "SetStdHandle", k_SetStdHandle },
    { "SetFilePointer", k_SetFilePointer },
    { "QueryPerformanceCounter", k_QueryPerformanceCounter },
    { "QueryPerformanceFrequency", k_QueryPerformanceFrequency },
    { "RaiseException", k_RaiseException }, { "RtlUnwind", k_RtlUnwind },
    { "FindResourceA", k_FindResourceA }, { "LoadResource", k_LoadResource },
    { "LockResource", k_Identity }, { "FreeResource", k_FreeNull },
    { "timeGetTime", k_timeGetTime }, { "DefDriverProc", k_DefDriverProc },
    { "RegOpenKeyExA", r_Open5 }, { "RegOpenKeyA", r_Open3 }, { "RegCreateKeyA", r_Create3 },
    { "RegCreateKeyExA", r_Create9 }, { "RegCloseKey", r_Close }, { "RegQueryValueExA", r_QueryEx },
    { "RegQueryValueA", r_Query4 }, { "RegSetValueExA", r_SetEx }, { "RegSetValueA", r_Set5 },
    { "RegDeleteKeyA", r_Del2 }, { "RegDeleteValueA", r_Del2 }, { "RegEnumKeyExA", r_Enum8 },
    { "wsprintfA", u_wsprintfA }, { "wvsprintfA", u_wvsprintfA }, { "LoadStringA", u_LoadStringA },
    { "MessageBoxA", u_MessageBoxA },
    { "CoInitialize", o_CoInitialize }, { "CoUninitialize", o_CoUninitialize },
    { "CoCreateInstance", o_CoCreateInstance }, { "CoTaskMemAlloc", o_CoTaskMemAlloc },
    { "CoTaskMemFree", o_CoTaskMemFree }, { "CoFreeUnusedLibraries", o_CoUninitialize },
    { 0, 0 }
};

/* unimplemented imports land in a per-import thunk that reports the name */
static const char *unimpl_names[512];
static void unimpl_hit(u32 idx)
{
    puts_fd(2, "iv5ref: unimplemented API called: ");
    puts_fd(2, idx < 512 && unimpl_names[idx] ? unimpl_names[idx] : "?");
    puts_fd(2, "\n");
    sys_exit(3);
}

/* ------------------------------------------------------------------ */
/* PE loader                                                           */

static u8 *read_file(const char *path, u32 *size)
{
    int fd = sys_open(path, 0, 0);
    u32 cap = 1 << 20, n = 0;
    u8 *buf;
    if (fd < 0) { puts_fd(2, path); die(": cannot open"); }
    buf = xmalloc(cap);
    for (;;) {
        int r;
        if (n == cap) { buf = xrealloc(buf, cap * 2); cap *= 2; }
        r = sys_read(fd, buf + n, cap - n);
        if (r <= 0) break;
        n += r;
    }
    sys_close(fd);
    *size = n;
    return buf;
}

static u32 load_pe(const char *path)
{
    u32 fsize, pe, nsec, opt, base, img_size, hdr_size, delta;
    u8 *f = read_file(path, &fsize), *img;
    u32 *dirs;
    int nunimpl = 0;
    u8 *thunks;

    pe       = *(u32 *)(f + 0x3c);
    nsec     = *(u16 *)(f + pe + 6);
    opt      = pe + 24;
    base     = *(u32 *)(f + opt + 28);
    img_size = *(u32 *)(f + opt + 56);
    hdr_size = *(u32 *)(f + opt + 60);
    dirs     = (u32 *)(f + opt + 96);

    img = sys_mmap((void *)base, img_size, 7, 0x22 | 0x100000 /* FIXED_NOREPLACE */);
    if ((u32)img > 0xfffff000u || (u32)img != base) {
        img = sys_mmap(0, img_size, 7, 0x22);
        if ((u32)img > 0xfffff000u) die("cannot map image");
    }
    memcpy(img, f, hdr_size);
    for (u32 i = 0; i < nsec; i++) {
        u8 *s = f + opt + *(u16 *)(f + pe + 20) + i * 40;
        u32 va = *(u32 *)(s + 12), rsz = *(u32 *)(s + 16), roff = *(u32 *)(s + 20);
        if (rsz) memcpy(img + va, f + roff, rsz);
    }
    /* relocations */
    delta = (u32)img - base;
    if (delta && dirs[5 * 2]) {
        u8 *r = img + dirs[5 * 2], *end = r + dirs[5 * 2 + 1];
        while (r < end) {
            u32 page = *(u32 *)r, bsz = *(u32 *)(r + 4);
            u16 *e = (u16 *)(r + 8);
            if (!bsz) break;
            for (u32 k = 0; k < (bsz - 8) / 2; k++)
                if ((e[k] >> 12) == 3) *(u32 *)(img + page + (e[k] & 0xfff)) += delta;
            r += bsz;
        }
    }
    /* imports */
    thunks = xmalloc(512 * 16);
    {
        u8 *d = img + dirs[1 * 2];
        for (; *(u32 *)(d + 12); d += 20) {
            u32 *ilt = (u32 *)(img + (*(u32 *)d ? *(u32 *)d : *(u32 *)(d + 16)));
            u32 *iat = (u32 *)(img + *(u32 *)(d + 16));
            for (; *ilt; ilt++, iat++) {
                const char *name = (*ilt & 0x80000000u) ? "#ordinal" : (const char *)(img + *ilt + 2);
                void *fn = 0;
                for (const struct api *a = apis; a->name; a++)
                    if (!memcmp(a->name, name, my_strlen(a->name) + 1)) { fn = a->fn; break; }
                if (!fn) {
                    /* push idx ; call unimpl_hit (never returns) */
                    u8 *t = thunks + nunimpl * 16;
                    unimpl_names[nunimpl] = name;
                    t[0] = 0x68; *(u32 *)(t + 1) = nunimpl;
                    t[5] = 0xe8; *(u32 *)(t + 6) = (u32)unimpl_hit - (u32)(t + 10);
                    fn = t;
                    nunimpl++;
                }
                *iat = (u32)fn;
            }
        }
    }
    dll_base = (u32)img;
    if (verbose) { puts_fd(2, "loaded at "); put_hex(2, (u32)img); puts_fd(2, ", unimplemented imports: "); put_dec(2, nunimpl); puts_fd(2, "\n"); }
    return (u32)img;
}

static u32 pe_export(u32 img, const char *name)
{
    u32 pe = *(u32 *)(img + 0x3c);
    u32 ed = *(u32 *)(img + pe + 24 + 96);
    u32 n = *(u32 *)(img + ed + 24);
    u32 *names = (u32 *)(img + *(u32 *)(img + ed + 32));
    u16 *ords  = (u16 *)(img + *(u32 *)(img + ed + 36));
    u32 *funcs = (u32 *)(img + *(u32 *)(img + ed + 28));
    for (u32 i = 0; i < n; i++)
        if (!memcmp((char *)(img + names[i]), name, my_strlen(name) + 1))
            return img + funcs[ords[i]];
    return 0;
}

/* Explicit diagnostic selection of Intel's scalar/MMX decoder path.
 * FUN_10034f70 copies this CPU setting from its instance into DAT_1009f088.
 * Patch the instance before that copy so reconstruction and synthesis agree.
 * The original DLL file is never changed. */
static int force_mmx = -1;
static u8 *cpu_tramp;
static u32 cpu_hook(u32 object)
{
    *(u32 *)(*(u32 *)(object + 4) + 0x18) = force_mmx;
    return ((u32 (*)(u32))cpu_tramp)(object);
}
static void install_cpu_override(u32 img)
{
    static const u8 prologue[6] = { 0x53, 0x56, 0x8b, 0x74, 0x24, 0x0c };
    u8 *f = (u8 *)(img + 0x34f70);
    if (memcmp(f, prologue, sizeof(prologue)))
        die("unsupported DLL for mmx override");
    cpu_tramp = xmalloc(32);
    memcpy(cpu_tramp, f, sizeof(prologue));
    cpu_tramp[6] = 0xe9;
    *(u32 *)(cpu_tramp + 7) = (u32)(f + 6) - (u32)(cpu_tramp + 11);
    f[0] = 0xe9; *(u32 *)(f + 1) = (u32)cpu_hook - (u32)(f + 5);
}

/* diagnostic hook on the internal status -> ICERR mapper (FUN_1004f7c0) */
static i32 status_hook(u32 st)
{
    static const i32 map[] = { -4, 0, -100, -401, -402, 1, -3, -6, -4, -201 };
    if (st != 1) { puts_fd(2, "  [codec] internal status "); put_dec(2, st); puts_fd(2, "\n"); }
    return st < 10 ? map[st] : (st == 0x15 ? 3 : -100);
}
/* diagnostic: log band skips (FUN_1001df60(ctx, plane, band)), then run the
 * original code via a trampoline holding its first instructions */
u8 *skip_tramp;  /* in RWX memory */
static int hook_len = 6;
__attribute__((used)) void skip_log(u32 ctx, u32 plane, u32 band);
__attribute__((used)) void skip_log(u32 ctx, u32 plane, u32 band)
{
    puts_fd(2, "  [codec] skip plane "); put_dec(2, plane); puts_fd(2, " band "); put_dec(2, band);
    puts_fd(2, " limit="); put_dec(2, *(u32 *)(ctx + 0x1d170));
    puts_fd(2, " flags128="); put_hex(2, *(u32 *)(ctx + 0x128));
    puts_fd(2, " mode(b8)="); put_dec(2, *(u32 *)(ctx + 0xb8));
    puts_fd(2, " bandflag="); put_hex(2, *(u8 *)(*(u32 *)(ctx + 0x17c + plane * 0x34) + 0x38 + band * 0x1aebc));
    puts_fd(2, "\n");
}
u8 *rc_tramp;  /* in RWX memory */
__attribute__((used)) void rc_log(u32 ctx, u32 a2, u32 a3, u32 a4, u32 a5, u32 nb);
__attribute__((used)) void rc_log(u32 ctx, u32 a2, u32 a3, u32 a4, u32 a5, u32 nb)
{
    (void)ctx; (void)a2; (void)a3; (void)a4; (void)a5;
    puts_fd(2, "  [codec] recompose bands="); put_dec(2, nb);
    puts_fd(2, " limit="); put_dec(2, *(u32 *)(ctx + 0x1d170));
    puts_fd(2, " load138="); put_dec(2, *(i32 *)(ctx + 0x1d138));
    puts_fd(2, " load15c="); put_dec(2, *(i32 *)(ctx + 0x1d15c));
    puts_fd(2, "\n");
}
__asm__(".globl rc_hook\nrc_hook:\n pushal\n pushfl\n"
        "  push 60(%esp)\n push 60(%esp)\n push 60(%esp)\n push 60(%esp)\n push 60(%esp)\n push 60(%esp)\n"
        "  call rc_log\n add $24, %esp\n popfl\n popal\n jmp *rc_tramp\n");
void rc_hook(void);
u8 *bd_tramp;  /* in RWX memory */
__attribute__((used)) void bd_log(u32 ctx, u32 data, u32 plen, u32 plane, u32 band, u32 recon);
__attribute__((used)) void bd_log(u32 ctx, u32 data, u32 plen, u32 plane, u32 band, u32 recon)
{
    (void)data; (void)plen;
    puts_fd(2, "  [codec] band decode plane "); put_dec(2, plane); puts_fd(2, " band "); put_dec(2, band);
    puts_fd(2, " recon="); put_dec(2, recon); puts_fd(2, " limit="); put_dec(2, *(u32 *)(ctx + 0x1d170));
    puts_fd(2, "\n");
}
__asm__(".globl bd_hook\nbd_hook:\n pushal\n pushfl\n"
        "  push 60(%esp)\n push 60(%esp)\n push 60(%esp)\n push 60(%esp)\n push 60(%esp)\n push 60(%esp)\n"
        "  call bd_log\n add $24, %esp\n popfl\n popal\n jmp *bd_tramp\n");
void bd_hook(void);
__asm__(".globl skip_hook\nskip_hook:\n"
        "  pushal\n pushfl\n push 48(%esp)\n push 48(%esp)\n push 48(%esp)\n call skip_log\n add $12, %esp\n"
        "  popfl\n popal\n jmp *skip_tramp\n");
void skip_hook(void);

static void install_hooks(u32 img)
{
    bd_tramp = xmalloc(32); rc_tramp = xmalloc(32); skip_tramp = xmalloc(32);
    {
        /* first instructions of FUN_1001df60 copied to the trampoline */
        u8 *f = (u8 *)(img + 0x1df60);
        int n = hook_len;
        memcpy(skip_tramp, f, n);
        skip_tramp[n] = 0xe9; *(u32 *)(skip_tramp + n + 1) = (u32)(f + n) - (u32)(skip_tramp + n + 5);
        f[0] = 0xe9; *(u32 *)(f + 1) = (u32)skip_hook - (u32)(f + 5);
    }
    {
        /* FUN_10025320: push ebp; mov ebp,esp; sub esp,0x3c (6 bytes) */
        u8 *f = (u8 *)(img + 0x25320);
        memcpy(bd_tramp, f, 6);
        bd_tramp[6] = 0xe9; *(u32 *)(bd_tramp + 7) = (u32)(f + 6) - (u32)(bd_tramp + 11);
        f[0] = 0xe9; *(u32 *)(f + 1) = (u32)bd_hook - (u32)(f + 5);
    }
    u8 *f = (u8 *)(img + 0x4f7c0);
    f[0] = 0xe9; *(u32 *)(f + 1) = (u32)status_hook - (u32)(f + 5);
}

/* FS segment pointing at a fake TEB */
static u32 teb[1024];
static void setup_fs(void)
{
    struct { u32 entry, base, limit, flags; } d;
    int sel;
    teb[0] = 0xffffffff;          /* SEH chain end */
    teb[1] = 0x00200000;          /* stack base (approx.) */
    teb[2] = 0;                   /* stack limit */
    teb[6] = (u32)teb;            /* self */
    teb[9] = 1234;                /* thread id */
    teb[11] = (u32)tls_slots;     /* TLS array */
    teb[12] = (u32)(teb + 512);   /* PEB */
    d.entry = -1; d.base = (u32)teb; d.limit = 0xfff;
    d.flags = 1 /* seg_32bit */ | (1 << 6) /* useable */;
    if (sys3(243, (int)&d, 0, 0) < 0) die("set_thread_area failed");
    sel = d.entry * 8 + 3;
    __asm__ volatile ("movw %w0, %%fs" :: "q"(sel));
}

/* ------------------------------------------------------------------ */
/* VfW driver interface                                                */

typedef WINAPI i32 (*DriverProcFn)(u32 id, u32 hdrv, u32 msg, u32 l1, u32 l2);
typedef WINAPI i32 (*DllMainFn)(u32 h, u32 reason, u32 res);

#define DRV_LOAD   1
#define DRV_ENABLE 2
#define DRV_OPEN   3
#define ICM_USER   0x4000
#define ICM_DECOMPRESS_QUERY (ICM_USER + 11)
#define ICM_DECOMPRESS_BEGIN (ICM_USER + 12)
#define ICM_DECOMPRESS       (ICM_USER + 13)
#define ICM_DECOMPRESS_END   (ICM_USER + 14)
#define FCC(a, b, c, d) ((u32)(a) | ((u32)(b) << 8) | ((u32)(c) << 16) | ((u32)(d) << 24))

struct bih {
    u32 biSize; i32 biWidth, biHeight; u16 biPlanes, biBitCount;
    u32 biCompression, biSizeImage; i32 biXPelsPerMeter, biYPelsPerMeter;
    u32 biClrUsed, biClrImportant;
};

static int atoi_(const char *s) { int v = 0; while (*s >= '0' && *s <= '9') v = v * 10 + *s++ - '0'; return v; }

/* ------------------------------------------------------------------ */
/* encoder driver                                                      */

#define ICM_COMPRESS_GET_FORMAT (ICM_USER + 4)
#define ICM_COMPRESS_GET_SIZE   (ICM_USER + 5)
#define ICM_COMPRESS_QUERY      (ICM_USER + 6)
#define ICM_COMPRESS_BEGIN      (ICM_USER + 7)
#define ICM_COMPRESS            (ICM_USER + 8)
#define ICM_COMPRESS_END        (ICM_USER + 9)
#define ICM_COMPRESS_FRAMES_INFO (ICM_USER + 70)
#define ICM_GETSTATE            0x5000
#define ICM_SETSTATE            0x5001
#define ICM_GETDEFAULTQUALITY   0x501e
#define ICM_GETDEFAULTKEYFRAMERATE (ICM_USER + 42)

static int opt_val(int argc, char **argv, int start, const char *key, int def)
{
    int l = my_strlen(key);
    for (int i = start; i < argc; i++)
        if (!memcmp(argv[i], key, l) && argv[i][l] == '=') {
            const char *v = argv[i] + l + 1; int neg = 0, r = 0;
            if (*v == '-') { neg = 1; v++; }
            if (v[0] == '0' && v[1] == 'x') {
                for (v += 2; *v; v++) r = r * 16 + (*v <= '9' ? *v - '0' : (*v | 32) - 'a' + 10);
            } else r = atoi_(v);
            return neg ? -r : r;
        }
    return def;
}

/* iv5ref [-v] enc DLL in.bgr24 W H out.bin [key=value ...]
 *   q=quality(0..10000) dr=datarate(bytes/s) kf=keyrate rate=N scale=M pad=0|1
 *   seqflags=ENCSEQ_* mask, key=, vpw=, vph=, tm=method, rlo/rhi/glo/ghi/blo/bhi=
 *   scal=, bidir=, rkb=, pp=, lat=, dead=, qc=, ver=0x10005 size=0x58
 *   fsize=dwFrameSize per frame, fq=dwQuality per frame, kfmask (force ICCOMPRESS_KEYFRAME every N) */
static int encode_main(int argc, char **argv, int a)
{
    u32 img, nframes, framebytes;
    DriverProcFn dp;
    struct bih in, out;
    u32 icopen[8] = { 32, FCC('v','i','d','c'), FCC('i','v','5','0'), 0x00010000, 1 /* ICMODE_COMPRESS */, 0, 0, 0 };
    i32 id, r;
    int w = atoi_(argv[a + 3]), h = atoi_(argv[a + 4]);
    u8 *src, *obuf, *prev = 0;
    int fd, infd, o = a + 6;
    u32 seq[32];
    u32 cf[16];

    setup_fs();
    {
        const char *pth = argv[a + 1]; int l = my_strlen(pth), k = l;
        while (k > 0 && pth[k - 1] != '/') k--;
        memcpy(dll_dir, pth, k); dll_dir[k] = 0;
    }
    img = load_pe(argv[a + 1]);
    if (verbose) install_hooks(img);
    {
        u32 pe = *(u32 *)(img + 0x3c);
        DllMainFn entry = (DllMainFn)(img + *(u32 *)(img + pe + 24 + 16));
        if (!entry(img, 1, 0)) die("DllMain failed");
    }
    dp = (DriverProcFn)pe_export(img, "DriverProc");
    dp(0, 1, DRV_LOAD, 0, 0);
    dp(0, 1, DRV_ENABLE, 0, 0);
    id = dp(0, 1, DRV_OPEN, 0, (u32)icopen);
    if (!id) die("DRV_OPEN (compress) failed");

    puts_fd(2, "default quality "); dp(id, 1, ICM_GETDEFAULTQUALITY, (u32)&r, 0); put_dec(2, r);
    puts_fd(2, ", default key rate "); r = -1; dp(id, 1, ICM_GETDEFAULTKEYFRAMERATE, (u32)&r, 0); put_dec(2, r); puts_fd(2, "\n");

    /* R4_ENC_SEQ_DATA */
    memset(seq, 0, sizeof(seq));
    seq[0] = opt_val(argc, argv, o, "size", 0x58);
    seq[1] = FCC('I','V','5','0');
    seq[2] = opt_val(argc, argv, o, "ver", 0x10005);
    seq[3] = 6;                               /* MT_ENCODE_SEQ_VALUE */
    seq[4] = 2;                               /* oeEnvironment: 32-bit */
    seq[5] = opt_val(argc, argv, o, "seqflags", 0) | 0x80000000u;
    seq[6] = opt_val(argc, argv, o, "key", 0);
    seq[7] = opt_val(argc, argv, o, "vpw", 0);
    seq[8] = opt_val(argc, argv, o, "vph", 0);
    ((u8 *)seq)[0x26] = opt_val(argc, argv, o, "blo", 0); ((u8 *)seq)[0x27] = opt_val(argc, argv, o, "bhi", 0);
    ((u8 *)seq)[0x28] = opt_val(argc, argv, o, "glo", 0); ((u8 *)seq)[0x29] = opt_val(argc, argv, o, "ghi", 0);
    ((u8 *)seq)[0x2a] = opt_val(argc, argv, o, "rlo", 0); ((u8 *)seq)[0x2b] = opt_val(argc, argv, o, "rhi", 0);
    seq[12] = opt_val(argc, argv, o, "tm", 1);
    if (seq[12] == 3) {
        /* TM_BITMASK: 1 bit per pixel, 0 = transparent, rows DWORD aligned;
         * left half of the picture transparent */
        int stride = ((w + 31) / 32) * 4;
        u8 *bm = xmalloc(stride * h);
        for (int y = 0; y < h; y++)
            for (int x = w / 2; x < w; x++)
                bm[y * stride + x / 8] |= 0x80 >> (x & 7);
        seq[11] = (u32)bm;
    }
    seq[13] = opt_val(argc, argv, o, "keyen", 0);
    seq[14] = opt_val(argc, argv, o, "scal", 0);
    seq[15] = opt_val(argc, argv, o, "bidir", 0);
    seq[16] = opt_val(argc, argv, o, "rkb", 0);
    seq[17] = opt_val(argc, argv, o, "pp", 2);
    ((u16 *)seq)[0x48 / 2] = opt_val(argc, argv, o, "lat", 0);
    ((u16 *)seq)[0x4a / 2] = opt_val(argc, argv, o, "dead", 0);
    seq[19] = opt_val(argc, argv, o, "qc", 0);
    seq[20] = opt_val(argc, argv, o, "x50", 0);
    seq[21] = opt_val(argc, argv, o, "x54", 0);
    if (seq[5] != 0x80000000u) {
        r = dp(id, 1, ICM_SETSTATE, (u32)seq, seq[0]);
        puts_fd(2, "ICM_SETSTATE(R4_ENC_SEQ_DATA) -> "); put_dec(2, r);
        puts_fd(2, " flags now "); put_hex(2, seq[5]); puts_fd(2, "\n");
    }

    memset(&in, 0, sizeof(in));
    in.biSize = 40; in.biWidth = w; in.biHeight = h; in.biPlanes = 1;
    in.biBitCount = opt_val(argc, argv, o, "bpp", 24);
    in.biSizeImage = ((w * in.biBitCount / 8 + 3) & ~3) * h;
    {
        int fcc = opt_val(argc, argv, o, "infcc", 0);   /* e.g. 0x39555659 YVU9 */
        if (fcc) { in.biCompression = fcc; in.biSizeImage = w * h * in.biBitCount / 8; }
        if (opt_val(argc, argv, o, "bihneg", 0)) in.biHeight = -h;
    }
    framebytes = in.biSizeImage;
    memset(&out, 0, sizeof(out));
    r = dp(id, 1, ICM_COMPRESS_GET_FORMAT, (u32)&in, (u32)&out);
    puts_fd(2, "GET_FORMAT -> "); put_dec(2, r); puts_fd(2, " out fcc "); put_hex(2, out.biCompression);
    puts_fd(2, " bpp "); put_dec(2, out.biBitCount); puts_fd(2, "\n");

    /* ICCOMPRESSFRAMES */
    memset(cf, 0, sizeof(cf));
    cf[0] = opt_val(argc, argv, o, "pad", 0);
    cf[1] = (u32)&out; cf[3] = (u32)&in;
    cf[5] = 0; cf[6] = 0x7fffffff;
    cf[7] = opt_val(argc, argv, o, "q", 8500);
    cf[8] = opt_val(argc, argv, o, "dr", 0);
    cf[9] = opt_val(argc, argv, o, "kf", 15);
    cf[10] = opt_val(argc, argv, o, "rate", 15);
    cf[11] = opt_val(argc, argv, o, "scale", 1);
    cf[12] = opt_val(argc, argv, o, "overhead", 0);
    r = dp(id, 1, ICM_COMPRESS_FRAMES_INFO, (u32)cf, 0x38);
    puts_fd(2, "FRAMES_INFO -> "); put_dec(2, r); puts_fd(2, "\n");

    r = dp(id, 1, ICM_COMPRESS_QUERY, (u32)&in, (u32)&out);
    puts_fd(2, "COMPRESS_QUERY -> "); put_dec(2, r); puts_fd(2, "\n");
    if (opt_val(argc, argv, o, "queryonly", 0)) {
        r = dp(id, 1, ICM_COMPRESS_QUERY, (u32)&in, 0);
        puts_fd(2, "COMPRESS_QUERY(no output fmt) -> "); put_dec(2, r); puts_fd(2, "\n");
        return 0;
    }
    r = dp(id, 1, ICM_COMPRESS_GET_SIZE, (u32)&in, (u32)&out);
    puts_fd(2, "GET_SIZE -> "); put_dec(2, r); puts_fd(2, "\n");
    obuf = xmalloc(r > 0 ? r + 4096 : framebytes * 2);
    r = dp(id, 1, ICM_COMPRESS_BEGIN, (u32)&in, (u32)&out);
    puts_fd(2, "COMPRESS_BEGIN -> "); put_dec(2, r); puts_fd(2, "\n");
    if (r) die("COMPRESS_BEGIN failed");

    {   /* read back the codec state after configuration */
        u32 st[32];
        memset(st, 0, sizeof(st));
        st[0] = 0x58; st[1] = FCC('I','V','5','0'); st[2] = 0x10005; st[3] = 6; st[4] = 2; st[5] = 0x800001ff;
        r = dp(id, 1, ICM_GETSTATE, (u32)st, 0x58);
        puts_fd(2, "GETSTATE(enc seq) -> "); put_dec(2, r); puts_fd(2, ":");
        for (int i = 5; i < 22; i++) { puts_fd(2, " "); put_hex(2, st[i]); }
        puts_fd(2, "\n");
    }

    /* stream the input frame by frame ("-" = stdin) */
    {
        const char *ip = argv[a + 2];
        infd = (ip[0] == '-' && !ip[1]) ? 0 : sys_open(ip, 0, 0);
        if (infd < 0) die("cannot open input");
    }
    src = xmalloc(framebytes * 2);
    nframes = 0x7fffffff;
    if (opt_val(argc, argv, o, "nf", 0) && (u32)opt_val(argc, argv, o, "nf", 0) < nframes) nframes = opt_val(argc, argv, o, "nf", 0);
    fd = sys_open(argv[a + 5], 01 | 0100 | 01000, 0644);
    for (u32 n = 0; n < nframes; n++) {
        u32 icc[12], ckid = 0, flags = 0;
        u8 *cur = src + (n & 1) * framebytes;
        u32 got = 0;
        while (got < framebytes) {
            int rr = sys_read(infd, cur + got, framebytes - got);
            if (rr <= 0) break;
            got += rr;
        }
        if (got < framebytes) { nframes = n; break; }
        int kfevery = opt_val(argc, argv, o, "kfmask", 0);
        memset(icc, 0, sizeof(icc));
        icc[0] = (kfevery && n % kfevery == 0) ? 1 : 0;     /* ICCOMPRESS_KEYFRAME */
        icc[1] = (u32)&out; icc[2] = (u32)obuf;
        icc[3] = (u32)&in;  icc[4] = (u32)cur;
        icc[5] = (u32)&ckid; icc[6] = (u32)&flags;
        icc[7] = n;
        icc[8] = opt_val(argc, argv, o, "fsize", 0);
        icc[9] = opt_val(argc, argv, o, "fq", 8500);
        icc[10] = n ? (u32)&in : 0; icc[11] = (u32)prev;
        out.biSizeImage = 0;
        r = dp(id, 1, ICM_COMPRESS, (u32)icc, sizeof(icc));
        if (r) { puts_fd(2, "frame "); put_dec(2, n); puts_fd(2, ": ICM_COMPRESS -> "); put_dec(2, r); puts_fd(2, "\n"); break; }
        {
            u32 len = out.biSizeImage;
            u32 hdr[2] = { len, flags };
            sys_write(fd, hdr, 8);
            sys_write(fd, obuf, len);
        }
        prev = cur;
    }
    dp(id, 1, ICM_COMPRESS_END, 0, 0);
    sys_close(fd);
    puts_fd(2, "iv5ref: encoded "); put_dec(2, nframes); puts_fd(2, " frames\n");
    return 0;
}

static u8 *tmask;
static u32 tmask_updated, tmask_size;
static int tfd = -1;

int main(int argc, char **argv)
{
    u32 img, fsize, pos = 0;
    DriverProcFn dp;
    struct bih in, out;
    u32 icopen[8] = { 32, FCC('v','i','d','c'), FCC('i','v','5','0'), 0x00010000, 2 /* ICMODE_DECOMPRESS */, 0, 0, 0 };
    u32 icd[6];
    i32 id, r;
    int w, h, rgb, fd, nframes = 0, outsize;
    u8 *frames, *obuf;
    int a = 1;

    if (argc > 1 && argv[1][0] == '-' && argv[1][1] == 'v') { verbose = 1; a++; }
    if (argc > a && !memcmp(argv[a], "enc", 4)) {
        if (argc - a < 6) die("usage: iv5ref [-v] enc DLL in.bgr24 W H out.bin [key=value ...]");
        return encode_main(argc, argv, a);
    }
    if (argc - a < 5) die("usage: iv5ref [-v] IR50_32.DLL frames.bin width height out.raw [rgb]");
    if (argc - a > 5 && !memcmp(argv[argc - 1], "mmx=", 4)) {
        const char *value = argv[argc - 1] + 4;
        if ((value[0] != '0' && value[0] != '1') || value[1])
            die("mmx must be 0 (scalar) or 1 (MMX)");
        force_mmx = value[0] - '0';
        argc--;
    }
    w = atoi_(argv[a + 2]); h = atoi_(argv[a + 3]);
    rgb = argc - a > 5;

    setup_fs();
    img = load_pe(argv[a]);
    if (force_mmx >= 0) install_cpu_override(img);
    if (verbose) install_hooks(img);
    {
        u32 pe = *(u32 *)(img + 0x3c);
        DllMainFn entry = (DllMainFn)(img + *(u32 *)(img + pe + 24 + 16));
        r = entry(img, 1, 0);
        if (verbose) { puts_fd(2, "DllMain -> "); put_dec(2, r); puts_fd(2, "\n"); }
        if (!r) die("DllMain failed");
    }
    dp = (DriverProcFn)pe_export(img, "DriverProc");
    if (!dp) die("no DriverProc export");

    dp(0, 1, DRV_LOAD, 0, 0);
    dp(0, 1, DRV_ENABLE, 0, 0);
    id = dp(0, 1, DRV_OPEN, 0, (u32)icopen);
    if (verbose) { puts_fd(2, "DRV_OPEN -> "); put_hex(2, id); puts_fd(2, "\n"); }
    if (!id) die("DRV_OPEN failed");

    memset(&in, 0, sizeof(in));
    in.biSize = 40; in.biWidth = w; in.biHeight = h; in.biPlanes = 1;
    in.biBitCount = 24; in.biCompression = FCC('I','V','5','0');
    in.biSizeImage = w * h * 3;
    out = in;
    if (rgb && argv[a + 5][0] != 'r') {
        /* explicit output FOURCC and bit count, e.g. YV12:12 */
        const char *f = argv[a + 5];
        out.biCompression = FCC(f[0], f[1], f[2], f[3]);
        out.biBitCount = atoi_(f + 5);
        outsize = w * h * out.biBitCount / 8;
    } else if (rgb) {
        out.biCompression = 0; out.biBitCount = 24;
        outsize = ((w * 3 + 3) & ~3) * h;
    } else {
        out.biCompression = FCC('Y','V','U','9'); out.biBitCount = 9;
        outsize = w * h + 2 * ((w + 3) / 4) * ((h + 3) / 4);
    }
    out.biSizeImage = outsize;

    r = dp(id, 1, ICM_DECOMPRESS_QUERY, (u32)&in, (u32)&out);
    if (verbose || r) { puts_fd(2, "ICM_DECOMPRESS_QUERY -> "); put_dec(2, r); puts_fd(2, "\n"); }
    if (r) die("output format rejected");
    /* optional access key: "key=<value>" anywhere after the output name */
    for (int i = a + 5; i < argc; i++)
        if (!memcmp(argv[i], "key=", 4)) {
            u32 sq[0x2c / 4];
            const char *v = argv[i] + 4;
            u32 k = 0;
            if (v[0] == '0' && v[1] == 'x')
                for (v += 2; *v; v++) k = k * 16 + (*v <= '9' ? *v - '0' : (*v | 32) - 'a' + 10);
            else
                k = atoi_(v);
            memset(sq, 0, sizeof(sq));
            sq[0] = 0x2c; sq[1] = FCC('I','V','5','0'); sq[2] = 0x10002;
            sq[3] = 4;                       /* MT_DECODE_SEQ_VALUE */
            sq[4] = 2;
            sq[5] = 0x80000000u | 1;         /* VALID | DECSEQ_KEY */
            sq[6] = k;                       /* dwKey */
            sq[7] = 1;                       /* fEnabledKey */
            r = dp(id, 1, 0x5001, (u32)sq, 0x2c);
            puts_fd(2, "SETSTATE(R4_DEC_SEQ_DATA key) -> "); put_dec(2, r); puts_fd(2, "\n");
            argc = i;                        /* not a mask file name */
        }

    r = dp(id, 1, ICM_DECOMPRESS_BEGIN, (u32)&in, (u32)&out);
    if (r) { puts_fd(2, "ICM_DECOMPRESS_BEGIN -> "); put_dec(2, r); die(""); }

    /* optional: ask for the transparency bitmask (R4_DEC_FRAME_DATA,
     * interface 1.2 layout, DECFRAME_TRANS_BITMASK) via env-free argv[a+6] */
    if (argc - a > 6) {
        u32 fd_[0x70 / 4];
        int bmstride = ((w + 31) / 32) * 4;
        tmask = xmalloc(bmstride * h + 64);
        memset(fd_, 0, sizeof(fd_));
        fd_[0] = 0x70; fd_[1] = FCC('I','V','5','0'); fd_[2] = 0x10002;
        fd_[3] = 2;                      /* MT_DECODE_FRAME_VALUE */
        fd_[4] = 2;                      /* 32-bit environment */
        fd_[5] = 0x80000000u | (1u << 4); /* VALID | DECFRAME_TRANS_BITMASK */
        fd_[0x40 / 4] = (u32)tmask;
        fd_[0x44 / 4] = (u32)&tmask_updated;
        r = dp(id, 1, 0x5001 /* ICM_SETSTATE */, (u32)fd_, 0x70);
        puts_fd(2, "SETSTATE(R4_DEC_FRAME_DATA trans bitmask) -> "); put_dec(2, r); puts_fd(2, "\n");
        tfd = sys_open(argv[a + 6], 01 | 0100 | 01000, 0644);
        tmask_size = bmstride * h;
    }

    frames = read_file(argv[a + 1], &fsize);
    obuf = xmalloc(outsize + 4096);
    /* i386 needs O_LARGEFILE: full-episode I420 output exceeds 2 GiB. */
    fd = sys_open(argv[a + 4], 01 | 0100 | 01000 | 0100000, 0644);
    if (fd < 0) die("cannot create output");

    while (pos + 4 <= fsize) {
        u32 len = *(u32 *)(frames + pos);
        u8 *data = xmalloc(len + 64);
        memcpy(data, frames + pos + 4, len);
        pos += 4 + len;
        in.biSizeImage = len;
        icd[0] = 0; icd[1] = (u32)&in; icd[2] = (u32)data;
        icd[3] = (u32)&out; icd[4] = (u32)obuf; icd[5] = 0;
        r = dp(id, 1, ICM_DECOMPRESS, (u32)icd, sizeof(icd));
        if (r) { puts_fd(2, "frame "); put_dec(2, nframes); puts_fd(2, ": ICM_DECOMPRESS -> "); put_dec(2, r); puts_fd(2, "\n"); }
        if (sys_write(fd, obuf, outsize) != outsize)
            die("cannot write decoded frame");
        if (tmask) {
            u32 upd = tmask_updated;
            sys_write(tfd, &upd, 4);
            sys_write(tfd, tmask, tmask_size);
        }
        nframes++;
    }
    dp(id, 1, ICM_DECOMPRESS_END, 0, 0);
    sys_close(fd);
    puts_fd(2, "iv5ref: decoded "); put_dec(2, nframes); puts_fd(2, " frames\n");
    return 0;
}

__asm__(".globl _start\n_start:\n"
        "  xor %ebp, %ebp\n"
        "  mov (%esp), %eax\n"
        "  lea 4(%esp), %ecx\n"
        "  and $-16, %esp\n"
        "  push %eax\n push %eax\n"
        "  push %ecx\n push %eax\n"
        "  call main\n"
        "  mov %eax, %ebx\n mov $1, %eax\n int $0x80\n");
