// Host QA harness for the GBA slice renderer: maps fake GBA regions,
// runs slice_main() in a thread, simulates VCOUNT, drives scripted taps
// via KEY, and dumps tiles + map + palette at scripted moments.
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <sys/mman.h>
#include <unistd.h>

extern int slice_main(void);

static void map_region(void *addr, size_t len) {
    void *p = mmap(addr, len, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
    if (p != addr) { perror("mmap"); exit(1); }
}
static volatile unsigned short *vcount_reg(void) { return (volatile unsigned short *)0x04000006; }
static volatile unsigned short *key_reg(void) { return (volatile unsigned short *)0x04000130; }
static unsigned char *sram(void) { return (unsigned char *)0x0E000000; }
static volatile int stop_flag = 0;
static void *vcount_thread(void *arg) {
    (void)arg; unsigned int v = 0;
    while (!stop_flag) { *vcount_reg() = (unsigned short)v; v = (v + 1) % 228; usleep(50); }
    return 0;
}
static void *game_thread(void *arg) { (void)arg; slice_main(); return 0; }
static void dump_shot(const char *name) {
    char path[256]; snprintf(path, sizeof path, "qa/%s.bin", name);
    FILE *f = fopen(path, "wb"); if (!f) return;
    fwrite((unsigned char *)0x06000000, 1, 4096, f);
    fwrite((unsigned char *)0x06008000, 1, 2048, f);
    fwrite((unsigned char *)0x05000000, 1, 512, f);
    fclose(f);
    printf("SHOT %s sram=%c%c state=%d pos=(%d,%d) ang=%d facing=%d frame=%d\n",
           name, sram()[0], sram()[1], sram()[2], sram()[3], sram()[4], sram()[5], sram()[8],
           sram()[6] * 256 + sram()[7]);
    fflush(stdout);
}
int main(void) {
    map_region((void *)0x04000000, 0x10000);
    map_region((void *)0x05000000, 0x1000);
    map_region((void *)0x06000000, 0x20000);
    map_region((void *)0x07000000, 0x1000);
    map_region((void *)0x0E000000, 0x10000);
    *key_reg() = 0x03FF;
    pthread_t gt, vt;
    pthread_create(&vt, 0, vcount_thread, 0);
    pthread_create(&gt, 0, game_thread, 0);
    for (int t = 0; t < 4000 && !stop_flag; t++) {
        unsigned short k = 0x03FF;
        if (t >= 200 && t < 220) k &= ~0x0008;
        else if (t >= 350 && t < 370) k &= ~0x0010;
        else if (t >= 500 && t < 520) k &= ~0x0040;
        else if (t >= 650 && t < 670) k &= ~0x0020;
        else if (t >= 800 && t < 820) k &= ~0x0040;
        *key_reg() = k;
        if (t == 150) dump_shot("shot-title");
        if (t == 280) dump_shot("shot-start");
        if (t == 470) dump_shot("shot-turn");
        if (t == 620) dump_shot("shot-move-south");
        if (t == 770) dump_shot("shot-turn-back");
        if (t == 920) dump_shot("shot-bump");
        usleep(5000);
    }
    dump_shot("shot-final");
    stop_flag = 1;
    printf("RESULT: DONE sram=%c%c state=%d pos=(%d,%d) ang=%d facing=%d frame=%d\n",
           sram()[0], sram()[1], sram()[2], sram()[3], sram()[4], sram()[5], sram()[8],
           sram()[6] * 256 + sram()[7]);
    fflush(stdout);
    _exit(0);
}
