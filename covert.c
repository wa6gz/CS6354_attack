#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>
#ifdef _MSC_VER
#include <intrin.h> /* for rdtscp and clflush */
#pragma optimize("gt",on)
#else
#include <x86intrin.h> /* for rdtscp and clflush */
#endif

// Access hardware timestamp counter
#define RDTSC(cycles) __asm__ volatile ("rdtsc" : "=a" (cycles));

// Serialize execution
#define CPUID() asm volatile ("CPUID" : : : "%rax", "%rbx", "%rcx", "%rdx");

// Intrinsic CLFLUSH for FLUSH+RELOAD attack
#define CLFLUSH(address) _mm_clflush(address);

#define SAMPLES 1000 // TODO: CONFIGURE THIS

#define L1_CACHE_SIZE (32*1024)
#define LINE_SIZE 64
#define ASSOCIATIVITY 8
#define L1_NUM_SETS (L1_CACHE_SIZE/(LINE_SIZE*ASSOCIATIVITY))
#define NUM_OFFSET_BITS 6
#define NUM_INDEX_BITS 6
#define NUM_OFF_IND_BITS (NUM_OFFSET_BITS + NUM_INDEX_BITS)
#define L2_TO_L1_SIZE 256

uint64_t eviction_counts[L1_NUM_SETS] = {0};
__attribute__ ((aligned (64))) uint64_t trojan_array[L2_TO_L1_SIZE*4096];
__attribute__ ((aligned (64))) uint64_t spy_array[4096];

uint64_t read_overhead;
uint64_t loop_overhead;

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

/* TODO:
 * This function provides an eviction set address, given the
 * base address of a trojan/spy array, the required cache
 * set ID, and way ID.
 *
 * Describe the algorithm used here.
 *
 *
 *
 *
 *
 *
 *
 *
 * For way 0, given a base address in a large block of memory, compute 
    the next valid memory location that falls in a given set.
    For way n (for 1-7), the function adds n to the tag bit to force
    the address to be the nth array location that maps to the given set. 
 */
uint64_t* get_eviction_set_address(uint64_t *base, int set, int way)
{
    uint64_t tag_bits = (((uint64_t)base) >> NUM_OFF_IND_BITS);
    int idx_bits = (((uint64_t)base) >> NUM_OFFSET_BITS) & 0x3f;

    if (idx_bits > set) {
        return (uint64_t *)((((tag_bits << NUM_INDEX_BITS) +
                               (L1_NUM_SETS + set)) << NUM_OFFSET_BITS) +
                            (L1_NUM_SETS * LINE_SIZE * way));
    } else {
        return (uint64_t *)((((tag_bits << NUM_INDEX_BITS) + set) << NUM_OFFSET_BITS) +
                            (L1_NUM_SETS * LINE_SIZE * way));
    }
}

/* This function sets up a trojan/spy eviction set using the
 * function above.  The eviction set is essentially a linked
 * list that spans all ways of the conflicting cache set.
 *
 * i.e., way-0 -> way-1 -> ..... way-7 -> NULL
 *
 */
void setup(uint64_t *base, int assoc)
{
    uint64_t i, j;
    uint64_t *eviction_set_addr;

    // Prime the cache set by set (i.e., prime all lines in a set)
    for (i = 0; i < L1_NUM_SETS; i++) {
        eviction_set_addr = get_eviction_set_address(base, i, 0);
        for (j = 1; j < assoc; j++) {
            *eviction_set_addr = (uint64_t)get_eviction_set_address(base, i, j);
            eviction_set_addr = (uint64_t *)*eviction_set_addr;
        }
        *eviction_set_addr = 0;
    }
}

/* TODO:
 *
 * This function implements the trojan that sends a message
 * to the spy over the cache covert channel.  Note that the
 * message forgoes case sensitivity to maximize the covert
 * channel bandwidth.
 *
 * Your job is to use the right eviction set to mount an
 * appropriate PRIME+PROBE or FLUSH+RELOAD covert channel
 * attack.  Remember that in both these attacks, we only need
 * to time the spy and not the trojan.
 *
 * Note that you may need to serialize execution wherever
 * appropriate.
 */
void trojan(char byte)
{
    int set, j;
    uint64_t *eviction_set_addr;
    // printf("%s\n", "in trojan with byte");

    if (byte >= 'a' && byte <= 'z') {
        byte -= 32;
    }
    if (byte == 10 || byte == 13) { // encode a new line
        set = 63;
    } else if (byte >= 32 && byte < 96) {
        set = (byte - 32);
    } else {
        printf("pp trojan: unrecognized character %c\n", byte);
        exit(1);
    }
    
    /* TODO:
     * Your attack code goes in here.
     * INSERT code from slides from evict and time
     */  

    //goal: communicate 6 bits
    //use 6 index bits as side channel
    //for char of value c, get base addr for set c
    //follow linked list to make access to set c
    //   and clear 
    //return


    // get base address
    eviction_set_addr = get_eviction_set_address(trojan_array, set, 0);
    //start reading at base address of set
    for (j = 1; j < L2_TO_L1_SIZE * ASSOCIATIVITY; j++) {
        //*eviction_set_addr = (uint64_t)get_eviction_set_address(trojan_array, byte, j);
        eviction_set_addr = (uint64_t *)*eviction_set_addr;
    }
    CPUID();
    //read entire linked list
}

/* TODO:
 *
 * This function implements the spy that receives a message
 * from the trojan over the cache covert channel.  Evictions
 * are timed using appropriate hardware timestamp counters
 * and recorded in the eviction_counts array.  In particular,
 * only record evictions to the set that incurred the maximum
 * penalty in terms of its access time.
 *
 * Your job is to use the right eviction set to mount an
 * appropriate PRIME+PROBE or FLUSH+RELOAD covert channel
 * attack.  Remember that in both these attacks, we only need
 * to time the spy and not the trojan.
 *
 * Note that you may need to serialize execution wherever
 * appropriate.
 */
char spy()
{
    int i, max_set, j;
    uint64_t *eviction_set_addr;
    uint64_t start, end, time, max_time = 0;
    // printf("%s\n", "in spy");

    // Probe the cache line by line and take measurements
    for (i = 0; i < L1_NUM_SETS; i++) {
        /* TODO:
         * Your attack code goes in here.
         *
         */  
        start = 0; end = 0; 
        //Start timer
        //RDTSC(start);
        // get base address
        eviction_set_addr = get_eviction_set_address(spy_array, i, 0);
        CPUID();
        RDTSC(start);
        //start reading at base address of set
        for (j = 1; j < ASSOCIATIVITY; j++) {
            //*eviction_set_addr = (uint64_t)get_eviction_set_address(spy_array, i, j);
            eviction_set_addr = (uint64_t *)*eviction_set_addr;
        }
        //read entire linked list
        //end timer
        RDTSC(end);
        CPUID();
        //if linked list took too long too access
        // set was accessed 
        time = end - start - read_overhead - (ASSOCIATIVITY-1) * loop_overhead;

        if(time > max_time){
            max_time = time;
            max_set = i;
        }


    }
    eviction_counts[max_set]++;
}

int main()
{
    FILE *in, *out;
    in = fopen("transmitted-secret.txt", "r");
    out = fopen("received-secret.txt", "w");

    int j, k;
    int max_count, max_set;

    // TODO: CONFIGURE THIS -- currently, 32*assoc to force eviction out of L2
    setup(trojan_array, ASSOCIATIVITY*L2_TO_L1_SIZE);

    setup(spy_array, ASSOCIATIVITY);

    read_overhead = compute_read_overhead();
    loop_overhead = compute_loop_overhead();
    
    for (;;) {
        char msg = fgetc(in);
        if (msg == EOF) {
            break;
        }
        for (k = 0; k < SAMPLES; k++) {
          trojan(msg);
          spy();
        }
        for (j = 0; j < L1_NUM_SETS; j++) {
            if (eviction_counts[j] > max_count) {
                max_count = eviction_counts[j];
                max_set = j;
            }
            eviction_counts[j] = 0;
        }
        if (max_set >= 33 && max_set <= 59) {
            max_set += 32;
        } else if (max_set == 63) {
            max_set = -22;
        }
        fprintf(out, "%c", 32 + max_set);
        fprintf(STDERR, "%c", 32 + max_set);
        max_count = max_set = 0;
    }
    fclose(in);
    fclose(out);
}
    