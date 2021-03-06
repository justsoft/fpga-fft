#include <owocomm/axi_fft.H>
#include "copy_array.H"

#include <stdio.h>
#include <stdint.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdexcept>
#include <complex>
#include <vector>

using namespace std;

#define MYFLAG_ROWTRANSPOSE (1<<5)
#define MYFLAG_INVERSE = (1<<6)


typedef int16_t s16;
typedef int32_t s32;
typedef int64_t s64;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint8_t u8;
typedef uint64_t u64;

static const long reservedMemAddr = 0x20000000;
static const long reservedMemSize = 0x10000000;
volatile uint8_t* reservedMem = NULL;
volatile uint8_t* reservedMemEnd = NULL;

// axi fft hardware parameters

typedef u64 fftWord;

// the width (cols) and height (rows) of the matrix in bursts
static const int W = 512, H = 512;

// the width and height of each burst in elements
static const int w = 2, h = 2;

// the size of the matrix in elements
static const int rows = H*h, cols = W*w;

// the total number of elements in the matrix
static const int N = rows*cols;

// the number of elements in each burst
static const int burstLength = w*h;

// the number of bytes occupied by the matrix
static const int sz = N*sizeof(fftWord);

OwOComm::AXIFFT* fft;

int mapReservedMem() {
	int memfd;
	if((memfd = open("/dev/mem", O_RDWR | O_SYNC)) < 0) {
		perror("open");
		printf( "ERROR: could not open /dev/mem\n" );
		return -1;
	}
	reservedMem = (volatile uint8_t*) mmap(NULL, reservedMemSize, ( PROT_READ | PROT_WRITE ), MAP_SHARED, memfd, reservedMemAddr);
	if(reservedMem == NULL) {
		perror("mmap");
		printf( "ERROR: could not map reservedMem\n" );
		return -1;
	}
	reservedMemEnd = reservedMem + reservedMemSize;
	close(memfd);
	return 0;
}

static inline uint64_t timespec_to_ns(const struct timespec *tv)
{
	return (uint64_t(tv->tv_sec) * 1000000000)
		+ (uint64_t)tv->tv_nsec;
}
int64_t operator-(const timespec& t1, const timespec& t2) {
	return int64_t(timespec_to_ns(&t1)-timespec_to_ns(&t2));
}


int myLog2(int n) {
	int res = (int)ceil(log2(n));
	assert(int(pow(2, res)) == n);
	return res;
}


// tests dma memory copy
void test1() {
	volatile uint64_t* srcArray = (volatile uint64_t*)reservedMem;
	volatile uint64_t* dstArray = (volatile uint64_t*)(reservedMem + sz);

	fft->pass1InFlags = AXIPIPE_FLAG_INTERLEAVE;
	fft->pass1OutFlags = AXIPIPE_FLAG_INTERLEAVE; // | AXIPIPE_FLAG_TRANSPOSE;

	for(int i=0; i<N; i++)
		srcArray[i] = i;

	for(int i=0; i<N; i++)
		dstArray[i] = 123;

	
	struct timespec startTime, endTime;
	clock_gettime(CLOCK_MONOTONIC, &startTime);
	fprintf(stderr, "dma start\n");

	for(int i=0; i<100; i++)
		fft->waitFFT(fft->submitFFT(srcArray, dstArray, false));
	
	clock_gettime(CLOCK_MONOTONIC, &endTime);
	fprintf(stderr, "dma end\n");
	fprintf(stderr, "total time %lld us\n", (endTime-startTime)/1000);

	// check results
	int errors = 0;
	for(int i=0; i<N; i++) {
		uint64_t result = dstArray[i];
		uint64_t expected = i;
		if(result != expected && (errors++) < 10) {
			printf("index %d: expected %llu, got %llu\n", i, expected, result);
		}
	}
}


int main(int argc, char** argv) {
	if(mapReservedMem() < 0) {
		return 1;
	}
	// the AXIFFT class can be used for any block processor attached to an axiPipe
	fft = new OwOComm::AXIFFT(0x43C00000, "/dev/uio0", 512,512,2,2);
	fft->reservedMem = reservedMem;
	fft->reservedMemEnd = reservedMemEnd;
	fft->reservedMemAddr = reservedMemAddr;
	fft->pass1InSize /= 4;

	test1();
	
	return 0;
}
