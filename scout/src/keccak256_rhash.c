/* sha3.c - an implementation of Secure Hash Algorithm 3 (Keccak).
 * based on the
 * The Keccak SHA-3 submission. Submission to NIST (Round 3), 2011
 * by Guido Bertoni, Joan Daemen, Michaël Peeters and Gilles Van Assche
 *
 * Copyright: 2013 Aleksey Kravchenko <rhash.admin@gmail.com>
 *
 * Permission is hereby granted,  free of charge,  to any person  obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction,  including without limitation
 * the rights to  use, copy, modify,  merge, publish, distribute, sublicense,
 * and/or sell copies  of  the Software,  and to permit  persons  to whom the
 * Software is furnished to do so.
 *
 * This program  is  distributed  in  the  hope  that it will be useful,  but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  Use this program  at  your own risk!
 */

/*
https://github.com/rhash/RHash/

main.c copied from:
  librhash/sha3.h
  librhash/sha3.c
  librhash/byteorder.c
  librhash/byteorder.h
  librhash/ustd.h

This can be replaced by an sha3 very easily, just use different functions in main
notes:
 - in rhash_keccak_init(), had to init stuff in custom way
*/


#include "ewasm.h"




// this is needed so that Keccak compiles

#define USE_KECCAK 1





// from librhash/byteorder.h

#define IS_ALIGNED_32(p) (0 == (3 & ((const char*)(p) - (const char*)0)))
#define IS_ALIGNED_64(p) (0 == (7 & ((const char*)(p) - (const char*)0)))

#define I64(x) x##ULL

#define RHASH_INLINE inline

void rhash_swap_copy_str_to_u32(void* to, int index, const void* from, size_t length);

static RHASH_INLINE uint32_t bswap_32(uint32_t x)
{
        x = ((x << 8) & 0xFF00FF00u) | ((x >> 8) & 0x00FF00FFu);
        return (x >> 16) | (x << 16);
}

# define be2me_32(x) bswap_32(x)
# define le2me_32(x) (x)
# define le2me_64(x) (x)
# define be32_copy(to, index, from, length) rhash_swap_copy_str_to_u32((to), (index), (from), (length))
# define me64_to_le_str(to, from, length) memcpy((to), (from), (length))


/* ROTL/ROTR macros rotate a 32/64-bit word left/right by n bits */
#define ROTL32(dword, n) ((dword) << (n) ^ ((dword) >> (32 - (n))))
#define ROTR32(dword, n) ((dword) >> (n) ^ ((dword) << (32 - (n))))
#define ROTL64(qword, n) ((qword) << (n) ^ ((qword) >> (64 - (n))))
#define ROTR64(qword, n) ((qword) >> (n) ^ ((qword) << (64 - (n))))







// from librhash/byteorder.c

void rhash_swap_copy_str_to_u32(void* to, int index, const void* from, size_t length)
{
        /* if all pointers and length are 32-bits aligned */
        if ( 0 == (( (int)((char*)to - (char*)0) | ((char*)from - (char*)0) | index | length ) & 3) ) {
                /* copy memory as 32-bit words */
                const uint32_t* src = (const uint32_t*)from;
                const uint32_t* end = (const uint32_t*)((const char*)src + length);
                uint32_t* dst = (uint32_t*)((char*)to + index);
                for (; src < end; dst++, src++)
                        *dst = bswap_32(*src);
        } else {
                const char* src = (const char*)from;
                for (length += index; (size_t)index < length; index++)
                        ((char*)to)[index ^ 3] = *(src++);
        }
}



// from librhash/sha3.h

#define sha3_224_hash_size  28
#define sha3_256_hash_size  32
#define sha3_384_hash_size  48
#define sha3_512_hash_size  64
#define sha3_max_permutation_size 25
#define sha3_max_rate_in_qwords 24

/**
 * SHA3 Algorithm context.
 */
typedef struct sha3_ctx
{
        /* 1600 bits algorithm hashing state */
        uint64_t hash[sha3_max_permutation_size];
        /* 1536-bit buffer for leftovers */
        uint64_t message[sha3_max_rate_in_qwords];
        /* count of bytes in the message[] buffer */
        unsigned rest;
        /* size of a message block processed at once */
        unsigned block_size;
} sha3_ctx;

/* methods for calculating the hash function */

void rhash_sha3_224_init(sha3_ctx *ctx);
void rhash_sha3_256_init(sha3_ctx *ctx);
void rhash_sha3_384_init(sha3_ctx *ctx);
void rhash_sha3_512_init(sha3_ctx *ctx);
void rhash_sha3_update(sha3_ctx *ctx, const unsigned char* msg, size_t size);
void rhash_sha3_final(sha3_ctx *ctx, unsigned char* result);

#ifdef USE_KECCAK
#define rhash_keccak_224_init rhash_sha3_224_init
#define rhash_keccak_256_init rhash_sha3_256_init
#define rhash_keccak_384_init rhash_sha3_384_init
#define rhash_keccak_512_init rhash_sha3_512_init
#define rhash_keccak_update rhash_sha3_update
void rhash_keccak_final(sha3_ctx *ctx, unsigned char* result);
#endif







// from librhash/sha3.c
// note: I comment out all assert() functions


/* constants */
#define NumberOfRounds 24

/* SHA3 (Keccak) constants for 24 rounds */
static uint64_t keccak_round_constants[NumberOfRounds] = {
	I64(0x0000000000000001), I64(0x0000000000008082), I64(0x800000000000808A), I64(0x8000000080008000),
	I64(0x000000000000808B), I64(0x0000000080000001), I64(0x8000000080008081), I64(0x8000000000008009),
	I64(0x000000000000008A), I64(0x0000000000000088), I64(0x0000000080008009), I64(0x000000008000000A),
	I64(0x000000008000808B), I64(0x800000000000008B), I64(0x8000000000008089), I64(0x8000000000008003),
	I64(0x8000000000008002), I64(0x8000000000000080), I64(0x000000000000800A), I64(0x800000008000000A),
	I64(0x8000000080008081), I64(0x8000000000008080), I64(0x0000000080000001), I64(0x8000000080008008)
};

/* Initializing a sha3 context for given number of output bits */
static void rhash_keccak_init(sha3_ctx *ctx, unsigned bits)
{
	/* NB: The Keccak capacity parameter = bits * 2 */
	unsigned rate = 1600 - bits * 2;

	// do this individually
	memset(ctx, 0, sizeof(sha3_ctx));
	//memset(ctx->hash, 0, sizeof(ctx->hash));
	//memset(ctx->message, 0, sizeof(ctx->message));
        //ctx->rest = 0;

	ctx->block_size = rate / 8;
	//assert(rate <= 1600 && (rate % 64) == 0);
}

/**
 * Initialize context before calculating hash.
 *
 * @param ctx context to initialize
 */
void rhash_sha3_224_init(sha3_ctx *ctx)
{
	rhash_keccak_init(ctx, 224);
}

/**
 * Initialize context before calculating hash.
 *
 * @param ctx context to initialize
 */
void rhash_sha3_256_init(sha3_ctx *ctx)
{
	rhash_keccak_init(ctx, 256);
}

/**
 * Initialize context before calculating hash.
 *
 * @param ctx context to initialize
 */
void rhash_sha3_384_init(sha3_ctx *ctx)
{
	rhash_keccak_init(ctx, 384);
}

/**
 * Initialize context before calculating hash.
 *
 * @param ctx context to initialize
 */
void rhash_sha3_512_init(sha3_ctx *ctx)
{
	rhash_keccak_init(ctx, 512);
}

#define XORED_A(i) A[(i)] ^ A[(i) + 5] ^ A[(i) + 10] ^ A[(i) + 15] ^ A[(i) + 20]
#define THETA_STEP(i) \
	A[(i)]      ^= D[(i)]; \
	A[(i) + 5]  ^= D[(i)]; \
	A[(i) + 10] ^= D[(i)]; \
	A[(i) + 15] ^= D[(i)]; \
	A[(i) + 20] ^= D[(i)] \

/* Keccak theta() transformation */
static void keccak_theta(uint64_t *A)
{
	uint64_t D[5];
	D[0] = ROTL64(XORED_A(1), 1) ^ XORED_A(4);
	D[1] = ROTL64(XORED_A(2), 1) ^ XORED_A(0);
	D[2] = ROTL64(XORED_A(3), 1) ^ XORED_A(1);
	D[3] = ROTL64(XORED_A(4), 1) ^ XORED_A(2);
	D[4] = ROTL64(XORED_A(0), 1) ^ XORED_A(3);
	THETA_STEP(0);
	THETA_STEP(1);
	THETA_STEP(2);
	THETA_STEP(3);
	THETA_STEP(4);
}

/* Keccak pi() transformation */
static void keccak_pi(uint64_t *A)
{
	uint64_t A1;
	A1 = A[1];
	A[ 1] = A[ 6];
	A[ 6] = A[ 9];
	A[ 9] = A[22];
	A[22] = A[14];
	A[14] = A[20];
	A[20] = A[ 2];
	A[ 2] = A[12];
	A[12] = A[13];
	A[13] = A[19];
	A[19] = A[23];
	A[23] = A[15];
	A[15] = A[ 4];
	A[ 4] = A[24];
	A[24] = A[21];
	A[21] = A[ 8];
	A[ 8] = A[16];
	A[16] = A[ 5];
	A[ 5] = A[ 3];
	A[ 3] = A[18];
	A[18] = A[17];
	A[17] = A[11];
	A[11] = A[ 7];
	A[ 7] = A[10];
	A[10] = A1;
	/* note: A[ 0] is left as is */
}

#define CHI_STEP(i) \
	A0 = A[0 + (i)]; \
	A1 = A[1 + (i)]; \
	A[0 + (i)] ^= ~A1 & A[2 + (i)]; \
	A[1 + (i)] ^= ~A[2 + (i)] & A[3 + (i)]; \
	A[2 + (i)] ^= ~A[3 + (i)] & A[4 + (i)]; \
	A[3 + (i)] ^= ~A[4 + (i)] & A0; \
	A[4 + (i)] ^= ~A0 & A1 \

/* Keccak chi() transformation */
static void keccak_chi(uint64_t *A)
{
	uint64_t A0, A1;
	CHI_STEP(0);
	CHI_STEP(5);
	CHI_STEP(10);
	CHI_STEP(15);
	CHI_STEP(20);
}

static void rhash_sha3_permutation(uint64_t *state)
{
	int round;
	for (round = 0; round < NumberOfRounds; round++)
	{
		keccak_theta(state);

		/* apply Keccak rho() transformation */
		state[ 1] = ROTL64(state[ 1],  1);
		state[ 2] = ROTL64(state[ 2], 62);
		state[ 3] = ROTL64(state[ 3], 28);
		state[ 4] = ROTL64(state[ 4], 27);
		state[ 5] = ROTL64(state[ 5], 36);
		state[ 6] = ROTL64(state[ 6], 44);
		state[ 7] = ROTL64(state[ 7],  6);
		state[ 8] = ROTL64(state[ 8], 55);
		state[ 9] = ROTL64(state[ 9], 20);
		state[10] = ROTL64(state[10],  3);
		state[11] = ROTL64(state[11], 10);
		state[12] = ROTL64(state[12], 43);
		state[13] = ROTL64(state[13], 25);
		state[14] = ROTL64(state[14], 39);
		state[15] = ROTL64(state[15], 41);
		state[16] = ROTL64(state[16], 45);
		state[17] = ROTL64(state[17], 15);
		state[18] = ROTL64(state[18], 21);
		state[19] = ROTL64(state[19],  8);
		state[20] = ROTL64(state[20], 18);
		state[21] = ROTL64(state[21],  2);
		state[22] = ROTL64(state[22], 61);
		state[23] = ROTL64(state[23], 56);
		state[24] = ROTL64(state[24], 14);

		keccak_pi(state);
		keccak_chi(state);

		/* apply iota(state, round) */
		*state ^= keccak_round_constants[round];
	}
}

/**
 * The core transformation. Process the specified block of data.
 *
 * @param hash the algorithm state
 * @param block the message block to process
 * @param block_size the size of the processed block in bytes
 */
static void rhash_sha3_process_block(uint64_t hash[25], const uint64_t *block, size_t block_size)
{
	/* expanded loop */
	hash[ 0] ^= le2me_64(block[ 0]);
	hash[ 1] ^= le2me_64(block[ 1]);
	hash[ 2] ^= le2me_64(block[ 2]);
	hash[ 3] ^= le2me_64(block[ 3]);
	hash[ 4] ^= le2me_64(block[ 4]);
	hash[ 5] ^= le2me_64(block[ 5]);
	hash[ 6] ^= le2me_64(block[ 6]);
	hash[ 7] ^= le2me_64(block[ 7]);
	hash[ 8] ^= le2me_64(block[ 8]);
	/* if not sha3-512 */
	if (block_size > 72) {
		hash[ 9] ^= le2me_64(block[ 9]);
		hash[10] ^= le2me_64(block[10]);
		hash[11] ^= le2me_64(block[11]);
		hash[12] ^= le2me_64(block[12]);
		/* if not sha3-384 */
		if (block_size > 104) {
			hash[13] ^= le2me_64(block[13]);
			hash[14] ^= le2me_64(block[14]);
			hash[15] ^= le2me_64(block[15]);
			hash[16] ^= le2me_64(block[16]);
			/* if not sha3-256 */
			if (block_size > 136) {
				hash[17] ^= le2me_64(block[17]);
#ifdef FULL_SHA3_FAMILY_SUPPORT
				/* if not sha3-224 */
				if (block_size > 144) {
					hash[18] ^= le2me_64(block[18]);
					hash[19] ^= le2me_64(block[19]);
					hash[20] ^= le2me_64(block[20]);
					hash[21] ^= le2me_64(block[21]);
					hash[22] ^= le2me_64(block[22]);
					hash[23] ^= le2me_64(block[23]);
					hash[24] ^= le2me_64(block[24]);
				}
#endif
			}
		}
	}
	/* make a permutation of the hash */
	rhash_sha3_permutation(hash);
}

#define SHA3_FINALIZED 0x80000000

/**
 * Calculate message hash.
 * Can be called repeatedly with chunks of the message to be hashed.
 *
 * @param ctx the algorithm context containing current hashing state
 * @param msg message chunk
 * @param size length of the message chunk
 */
void rhash_sha3_update(sha3_ctx *ctx, const unsigned char *msg, size_t size)
{
	size_t index = (size_t)ctx->rest;
	size_t block_size = (size_t)ctx->block_size;

	if (ctx->rest & SHA3_FINALIZED) return; /* too late for additional input */
	ctx->rest = (unsigned)((ctx->rest + size) % block_size);

	/* fill partial block */
	if (index) {
		size_t left = block_size - index;
		memcpy((char*)ctx->message + index, msg, (size < left ? size : left));
		if (size < left) return;

		/* process partial block */
		rhash_sha3_process_block(ctx->hash, ctx->message, block_size);
		msg  += left;
		size -= left;
	}
	while (size >= block_size) {
		uint64_t* aligned_message_block;
		if (IS_ALIGNED_64(msg)) {
			/* the most common case is processing of an already aligned message
			without copying it */
			aligned_message_block = (uint64_t*)msg;
		} else {
			memcpy(ctx->message, msg, block_size);
			aligned_message_block = ctx->message;
		}

		rhash_sha3_process_block(ctx->hash, aligned_message_block, block_size);
		msg  += block_size;
		size -= block_size;
	}
	if (size) {
		memcpy(ctx->message, msg, size); /* save leftovers */
	}
}

/**
 * Store calculated hash into the given array.
 *
 * @param ctx the algorithm context containing current hashing state
 * @param result calculated hash in binary form
 */
void rhash_sha3_final(sha3_ctx *ctx, unsigned char* result)
{
	size_t digest_length = 100 - ctx->block_size / 2;
	const size_t block_size = ctx->block_size;

	if (!(ctx->rest & SHA3_FINALIZED))
	{
		/* clear the rest of the data queue */
		memset((char*)ctx->message + ctx->rest, 0, block_size - ctx->rest);
		((char*)ctx->message)[ctx->rest] |= 0x06;
		((char*)ctx->message)[block_size - 1] |= 0x80;

		/* process final block */
		rhash_sha3_process_block(ctx->hash, ctx->message, block_size);
		ctx->rest = SHA3_FINALIZED; /* mark context as finalized */
	}

	//assert(block_size > digest_length);
	if (result) me64_to_le_str(result, ctx->hash, digest_length);
}

#ifdef USE_KECCAK
/**
* Store calculated hash into the given array.
*
* @param ctx the algorithm context containing current hashing state
* @param result calculated hash in binary form
*/
void rhash_keccak_final(sha3_ctx *ctx, unsigned char* result)
{
	size_t digest_length = 100 - ctx->block_size / 2;
	const size_t block_size = ctx->block_size;

	if (!(ctx->rest & SHA3_FINALIZED))
	{
		/* clear the rest of the data queue */
		memset((char*)ctx->message + ctx->rest, 0, block_size - ctx->rest);
		((char*)ctx->message)[ctx->rest] |= 0x01;
		((char*)ctx->message)[block_size - 1] |= 0x80;

		/* process final block */
		rhash_sha3_process_block(ctx->hash, ctx->message, block_size);
		ctx->rest = SHA3_FINALIZED; /* mark context as finalized */
	}

	//assert(block_size > digest_length);
	if (result) me64_to_le_str(result, ctx->hash, digest_length);
}
#endif /* USE_KECCAK */







/*
This is awkward for now. 
LLVM generated Wasm has a stack which grows down. We want to avoid things being put on this stack.
So we generate a buffer in global scope, which gets mapped by LLVM to Wasm memory.
In May 2019, LLVM to wasm does not yet support global arrays which are not initialized like this, see:
https://github.com/llvm-mirror/llvm/blob/c44c327530160e1567da37e2e5877d03d8af4c8a/lib/MC/MCWasmStreamer.cpp#L137-L149
*/
uint8_t buffer[] = { //432 total bytes, enough for out and sha3_ctx
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,  
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
};

void _main(){

  int length = eth2_blockDataSize(); //length in bytes
  unsigned char* in = (unsigned char*) malloc(length);
  eth2_blockDataCopy( (uint32_t*)in, 0, length ); //get data to hash into memory
  unsigned char* out = buffer;

#if 1	// for benchmarking
  int loop_iters = (50000 + (length - 1)) / (length + 1);
  int ret;
  for (int i=0; i<loop_iters; i++){
    sha3_ctx* ctx = (sha3_ctx*) buffer+32;
    rhash_keccak_256_init(ctx);
    rhash_keccak_update(ctx, in, length);
    rhash_keccak_final(ctx, out);
  }
#else
  sha3_ctx* ctx = (sha3_ctx*) buffer+32;
  rhash_keccak_256_init(ctx);
  rhash_keccak_update(ctx, in, length);
  rhash_keccak_final(ctx, out);
#endif

  eth2_savePostStateRoot((i32ptr*)out);

}
/*
void _main(){

  int length = getCallDataSize(); //length in bytes
  unsigned char* in = (unsigned char*) malloc( length * sizeof(unsigned char));
  callDataCopy( (i32ptr*)in, 0, length ); //get data to hash into memory
  unsigned char out[32];

  sha3_ctx ctx;
  rhash_keccak_256_init(&ctx);
  rhash_keccak_update(&ctx, in, length);
  rhash_keccak_final(&ctx, out);

  finish((i32ptr*)out,32);

}
*/
