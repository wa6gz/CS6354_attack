#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>

#define RDTSC(cycles) __asm__ volatile ("rdtsc" : "=a" (cycles));

#define SAMPLES 1000000

uint64_t read_overhead;
uint64_t loop_overhead;


uint64_t large_array[1024*1024*1024], *p;

uint64_t compute_read_overhead()
{
    uint64_t start = 0, end = 0;
    RDTSC(start);
    RDTSC(end);
    return (end - start);
}

uint64_t compute_loop_overhead()
{
    uint64_t start = 0, end = 0, i;
    RDTSC(start);
    for (i = 0; i < SAMPLES; i++) {
    }
    RDTSC(end);
    return (end - start - read_overhead) / SAMPLES;
}

uint64_t cache_fill(uint64_t size)
{
    uint64_t start = 0, end = 0, j, nelems_per_line = 8;
    uint64_t diff = 0, i, nelems = 0;

    nelems = size / nelems_per_line;

    // Prime the cache line by line (accounting for potential prefetching)
    for (i = 0; i <= nelems; i += nelems_per_line) {
        large_array[i] = (uint64_t)&large_array[(i + 4*nelems_per_line) % nelems];
    }

    // Probe the cache line by line and take measurements
    p = (uint64_t *) large_array[0];
    RDTSC(start);
    for (j = 0; j < SAMPLES; j++){
        p = (uint64_t *)*p;
    }
    RDTSC(end);

    diff = (end - start - (loop_overhead * SAMPLES) - read_overhead);
    return (diff/SAMPLES);
}

double iq_lsq_rob_fill(int size)
{
    uint64_t start = 0, end = 0, j, k, nelems_per_line = 8;
    uint64_t diff = 0, i, nelems = 0;

    // Extreme priming
    nelems = (256*1024*1024) / nelems_per_line;
    for (i = 0; i <= nelems; i += nelems_per_line) {
        large_array[i] = (uint64_t)&large_array[(i + 4*nelems_per_line) % nelems];
    }
    
    p = (uint64_t *) large_array[0];

    //5 instructions in each iteration
    int loop_cnt = (size-10)/5;
    __asm__ volatile ("push %r8");
    RDTSC(start);
    for (j = 0; j < SAMPLES; j++){
        p = (uint64_t *)*p;
        p = (uint64_t *)*p;
        p = (uint64_t *)*p;
        __asm__ volatile ("movq $0, %r8");
        __asm__ volatile ("loop: addq $1, %r8");
        __asm__ volatile ("nop");
        __asm__ volatile ("cmpq %%r8, %0": "=m"(loop_cnt));
        __asm__ volatile ("jb loop");
    }
    RDTSC(end);
    __asm__ volatile ("pop %r8");

    diff = (end - start - (loop_overhead * SAMPLES) - read_overhead);

    double cpi =  (double)diff/(double)(size*SAMPLES);
    return cpi;
}

int main()
{
    read_overhead = compute_read_overhead();
    printf("read overhead: %2llu cycles\n", read_overhead);

    loop_overhead = compute_loop_overhead();
    printf("loop overhead: %2llu cycles\n", loop_overhead);

    uint64_t size;
    cache_fill(16*1024);
    for (size = 4096; size < (1024*1024*1024); size *= 2) {
        uint64_t cycles = cache_fill(size);
        if (cycles > 500) { // potential rdtsc overflow
            cycles = cache_fill(size);
        }
        printf("size: %llu\t %llu cycles\n", size, cycles);
    }

    for (size = 400; size >= 20; size -= 10) {
        double cpi = iq_lsq_rob_fill(size);
        if (cpi > 5) { // potential rdtsc overflow
            cpi = iq_lsq_rob_fill(size);
        }
        printf("size: %llu\t cpi: %lf\n", size, cpi);
    }
}
