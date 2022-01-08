#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <linux/io_uring.h>
#include <time.h>

typedef uint8_t u8;
typedef uint32_t u32;

int io_uring_setup(unsigned entries, struct io_uring_params *params){
	return (int)syscall(SYS_io_uring_setup, entries, params);
}

int io_uring_enter(int ring_fd, unsigned to_submit, unsigned min_complete, unsigned flags){
	return (int)syscall(SYS_io_uring_enter, ring_fd, to_submit, min_complete, flags, NULL, 0);
}


void ms_sleep(unsigned ms){
	struct timespec ts;
	ts.tv_sec = (ms / 1000);
	ts.tv_nsec = (ms % 1000) * 1000000;
	nanosleep(&ts, NULL);
}

#define USE_SQPOLL 1

int main(int argc, char **argv){
	struct io_uring_params params;
	memset(&params, 0, sizeof(struct io_uring_params));

#if USE_SQPOLL
	params.flags |= IORING_SETUP_SQPOLL;
	params.sq_thread_idle = 500;
#endif

	int ring_fd = io_uring_setup(32, &params);
	if(ring_fd == - 1){
		printf("Failed to setup io_uring (errno = %d)\n", errno);
		return -1;
	}
	printf("sizeof(short): %zu\n", sizeof(short));
	printf("sizeof(int): %zu\n", sizeof(int));
	printf("sizeof(long): %zu\n", sizeof(long));
	printf("sizeof(long long): %zu\n", sizeof(long long));
	printf("sizeof(struct io_uring_sqe): %zu\n", sizeof(struct io_uring_sqe));
	printf("sizeof(struct io_uring_cqe): %zu\n", sizeof(struct io_uring_cqe));
	printf("sq_entries: %u\n", params.sq_entries);
	printf("cq_entries: %u\n", params.cq_entries);
	printf("features: %08X\n", params.features);

	printf("sq_off:\n");
	printf("\thead: %u\n", params.sq_off.head);
	printf("\ttail: %u\n", params.sq_off.tail);
	printf("\tring_mask: %u\n", params.sq_off.ring_mask);
	printf("\tring_entries: %u\n", params.sq_off.ring_entries);
	printf("\tflags: %u\n", params.sq_off.flags);
	printf("\tdropped: %u\n", params.sq_off.dropped);
	printf("\tarray: %u\n", params.sq_off.array);
	printf("\tresv1: %u\n", params.sq_off.resv1);
	printf("\tresv2: %llu\n", params.sq_off.resv2);

	printf("cq_off:\n");
	printf("\thead: %u\n", params.cq_off.head);
	printf("\ttail: %u\n", params.cq_off.tail);
	printf("\tring_mask: %u\n", params.cq_off.ring_mask);
	printf("\tring_entries: %u\n", params.cq_off.ring_entries);
	printf("\toverflow: %u\n", params.cq_off.overflow);
	printf("\tcqes: %u\n", params.cq_off.cqes);
	printf("\tflags: %u\n", params.cq_off.flags);
	printf("\tresv1: %u\n", params.cq_off.resv1);
	printf("\tresv2: %llu\n", params.cq_off.resv2);

	bool single_mmap = (params.features & IORING_FEAT_SINGLE_MMAP) != 0;

	u32 sq_entries = params.sq_entries;
	u32 cq_entries = params.cq_entries;

	u32 sq_ring_size = params.sq_off.array + sq_entries * sizeof(u32);
	u32 cq_ring_size = params.cq_off.cqes + cq_entries * sizeof(struct io_uring_cqe);

	u32 sqes_size = sq_entries * sizeof(struct io_uring_sqe);

	// NOTE: We are not enforced to do a single mmap if the feature is present. If we
	// do one mmap for each of the rings, we'll get the same underlying memory (perhaps
	// with different virtual addresses but pointing to the same physical memory).
	if(single_mmap){
		if(sq_ring_size > cq_ring_size){
			cq_ring_size = sq_ring_size;
		}else{
			sq_ring_size = sq_ring_size;
		}
	}

	u8 *sq_ring, *cq_ring;
	struct io_uring_sqe *sqes;

	sq_ring = (u8*)mmap(NULL, sq_ring_size, PROT_READ | PROT_WRITE,
		MAP_SHARED | MAP_POPULATE, ring_fd, IORING_OFF_SQ_RING);
	if(sq_ring == MAP_FAILED){
		printf("failed to map sq ring (errno = %d)\n", errno);
		goto err;
	}

	if(single_mmap){
		cq_ring = sq_ring;
	}else{
		cq_ring = (u8*)mmap(NULL, cq_ring_size, PROT_READ | PROT_WRITE,
			MAP_SHARED | MAP_POPULATE, ring_fd, IORING_OFF_CQ_RING);
		if(cq_ring == MAP_FAILED){
			printf("failed to map cq ring (errno = %d)\n", errno);
			goto err;
		}
	}

	sqes = (struct io_uring_sqe*)mmap(NULL, sqes_size, PROT_READ | PROT_WRITE,
		MAP_SHARED | MAP_POPULATE, ring_fd, IORING_OFF_SQES);
	if(sqes == MAP_FAILED){
		printf("failed to map sqes (errno = %d)\n", errno);
		goto err;
	}


	u32 *sq_head, *sq_tail, *sq_ring_mask, *sq_ring_entries, *sq_flags, *sq_dropped, *sq_array;
	sq_head = (u32*)(sq_ring + params.sq_off.head);
	sq_tail = (u32*)(sq_ring + params.sq_off.tail);
	sq_ring_mask = (u32*)(sq_ring + params.sq_off.ring_mask);
	sq_ring_entries = (u32*)(sq_ring + params.sq_off.ring_entries);
	sq_flags = (u32*)(sq_ring + params.sq_off.flags);
	sq_dropped = (u32*)(sq_ring + params.sq_off.dropped);
	sq_array = (u32*)(sq_ring + params.sq_off.array);


	u32 *cq_head, *cq_tail, *cq_ring_mask, *cq_ring_entries, *cq_flags, *cq_overflow;
	struct io_uring_cqe *cqes;
	cq_head = (u32*)(cq_ring + params.cq_off.head);
	cq_tail = (u32*)(cq_ring + params.cq_off.tail);
	cq_ring_mask = (u32*)(cq_ring + params.cq_off.ring_mask);
	cq_ring_entries = (u32*)(cq_ring + params.cq_off.ring_entries);
	cq_flags = (u32*)(cq_ring + params.cq_off.flags);
	cq_overflow = (u32*)(cq_ring + params.cq_off.overflow);
	cqes = (struct io_uring_cqe*)(cq_ring + params.cq_off.cqes);

	while(1){
		u32 head, tail, mask, num_entries;

		// NOTE: consume completions
		tail = __atomic_load_n(cq_tail, __ATOMIC_ACQUIRE);
		head = *cq_head;
		mask = *cq_ring_mask;
		num_entries = tail - head;
		if(num_entries > 0){
			for(u32 i = 0; i < num_entries; i += 1){
				u32 cqe_index = (head + i) & mask;
				printf("cqe %d: user_data = %llu, res = %d, flags = %u\n",
					cqe_index, cqes[cqe_index].user_data,
					cqes[cqe_index].res, cqes[cqe_index].flags);
			}
			__atomic_store_n(cq_head, tail, __ATOMIC_RELEASE);
		}

		// NOTE: push submissions
		head = __atomic_load_n(sq_head, __ATOMIC_ACQUIRE);
		tail = *sq_tail;
		mask = *sq_ring_mask;
		num_entries = tail - head;
		if(num_entries < *sq_ring_entries){
			u32 entries_to_fill = (*sq_ring_entries - num_entries);
			for(u32 i = 0; i < entries_to_fill; i += 1){
				u32 sqe_index = (tail + i) & mask;
				memset(&sqes[sqe_index], 0, sizeof(struct io_uring_sqe));
				sqes[sqe_index].opcode = IORING_OP_NOP;
				sqes[sqe_index].user_data = tail + i;
				sq_array[sqe_index] = sqe_index;
				printf("sqe %d: user_data = %llu\n",
					sqe_index, sqes[sqe_index].user_data);
			}

			__atomic_store_n(sq_tail, (tail + entries_to_fill), __ATOMIC_RELEASE);

#if USE_SQPOLL
			u32 flags = __atomic_load_n(sq_flags, __ATOMIC_RELAXED);
			if(flags & IORING_SQ_NEED_WAKEUP){
				int ret = io_uring_enter(ring_fd, 0, 0, IORING_ENTER_SQ_WAKEUP);
				if(ret < 0){
					printf("io_uring_enter failed (errno = %d)\n", errno);
				}
			}
#else
			int ret = io_uring_enter(ring_fd, entries_to_fill, 0, 0);
			if(ret < 0){
				printf("io_uring_enter failed (errno = %d)\n", errno);
			}

#endif
		}

		printf("============================\n");
		printf("SQ STATUS:\n");
		printf("\tsq_head = %u\n", __atomic_load_n(sq_head, __ATOMIC_RELAXED));
		printf("\tsq_tail = %u\n", __atomic_load_n(sq_tail, __ATOMIC_RELAXED));
		printf("\tsq_ring_mask = %u\n", __atomic_load_n(sq_ring_mask, __ATOMIC_RELAXED));
		printf("\tsq_ring_entries = %u\n", __atomic_load_n(sq_ring_entries, __ATOMIC_RELAXED));
		printf("\tsq_flags = %08X\n", __atomic_load_n(sq_flags, __ATOMIC_RELAXED));
		printf("\tsq_dropped = %u\n", __atomic_load_n(sq_dropped, __ATOMIC_RELAXED));
		printf("CQ STATUS:\n");
		printf("\tcq_head = %u\n", __atomic_load_n(cq_head, __ATOMIC_RELAXED));
		printf("\tcq_tail = %u\n", __atomic_load_n(cq_tail, __ATOMIC_RELAXED));
		printf("\tcq_ring_mask = %u\n", __atomic_load_n(cq_ring_mask, __ATOMIC_RELAXED));
		printf("\tcq_ring_entries = %u\n", __atomic_load_n(cq_ring_entries, __ATOMIC_RELAXED));
		printf("\tcq_flags = %08X\n", __atomic_load_n(cq_flags, __ATOMIC_RELAXED));
		printf("\tcq_overflow = %u\n", __atomic_load_n(cq_overflow, __ATOMIC_RELAXED));
		printf("============================\n");

		ms_sleep(200);
	}
err:
	close(ring_fd);
	return 0;
}

