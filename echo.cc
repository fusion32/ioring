#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <linux/io_uring.h>
#include <time.h>


#define USE_SQPOLL 1

#define PORT 7171

typedef uint8_t u8;
typedef int32_t i32;
typedef uint32_t u32;
typedef uint64_t u64;

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


struct echo{
	u32 opcode;
	struct msghdr msg;
	struct sockaddr_in client;
	struct iovec iov;
	char buf[2048];
};

int main(int argc, char **argv){
	struct io_uring_params params;
	memset(&params, 0, sizeof(struct io_uring_params));

#if USE_SQPOLL
	params.flags |= IORING_SETUP_SQPOLL;
	params.sq_thread_idle = 100;
#endif

	int ring_fd = io_uring_setup(32, &params);
	if(ring_fd == - 1){
		printf("Failed to setup io_uring (errno = %d)\n", errno);
		return -1;
	}

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
		return -1;
	}

	if(single_mmap){
		cq_ring = sq_ring;
	}else{
		cq_ring = (u8*)mmap(NULL, cq_ring_size, PROT_READ | PROT_WRITE,
			MAP_SHARED | MAP_POPULATE, ring_fd, IORING_OFF_CQ_RING);
		if(cq_ring == MAP_FAILED){
			printf("failed to map cq ring (errno = %d)\n", errno);
			return -1;
		}
	}

	sqes = (struct io_uring_sqe*)mmap(NULL, sqes_size, PROT_READ | PROT_WRITE,
		MAP_SHARED | MAP_POPULATE, ring_fd, IORING_OFF_SQES);
	if(sqes == MAP_FAILED){
		printf("failed to map sqes (errno = %d)\n", errno);
		return -1;
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

	// NOTE: We are adding yet another indirection with this echo_sq just to avoid
	// scrambling too much stuff. When we factor out the ioring stuff to other functions,
	// we probably won't need it.

	struct echo *echos = (struct echo*)malloc(sq_entries * sizeof(struct echo));
	if(echos == NULL){
		printf("failed to allocate echos\n");
		return -1;
	}

	u32 echo_sq_num = sq_entries;
	u32 *echo_sq = (u32*)malloc(2 * sq_entries * sizeof(u32));
	if(echo_sq == NULL){
		printf("failed to allocate echo_sq\n");
		return -1;
	}

	for(u32 i = 0; i < sq_entries; i += 1){
		echo_sq[i] = i;

		echos[i].opcode = IORING_OP_RECVMSG;

		echos[i].iov.iov_base = echos[i].buf;
		echos[i].iov.iov_len = sizeof(echos[i].buf);

		echos[i].msg.msg_name = &echos[i].client;
		echos[i].msg.msg_namelen = sizeof(echos[i].client);
		echos[i].msg.msg_iov = &echos[i].iov;
		echos[i].msg.msg_iovlen = 1;
		echos[i].msg.msg_control = NULL;
		echos[i].msg.msg_controllen = 0;
		echos[i].msg.msg_flags = 0;
	}

	// NOTE: socket stuff
	int s = socket(AF_INET, SOCK_DGRAM, 0);
	if(s == -1){
		printf("failed to create socket (errno = %d)\n", errno);
		return -1;
	}

	{
		int reuseaddr = 1;
		int ret = setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &reuseaddr, sizeof(int));
		if(ret == -1){
			printf("failed to set socket option SO_REUSEADDR (errno = %d)\n", errno);
			return -1;
		}
	}
	
	{
		struct sockaddr_in addr;
		addr.sin_family = AF_INET;
		addr.sin_port = htons(PORT);
		addr.sin_addr.s_addr = htons(INADDR_ANY);
		int ret = bind(s, (struct sockaddr*)&addr, sizeof(struct sockaddr_in));
		if(ret == -1){
			printf("failed to bind socket to port %d (errno = %d)\n", PORT, errno);
			return -1;
		}
	}

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
				u32 echo_index = (u32)cqes[cqe_index].user_data;
				i32 result = cqes[cqe_index].res;
				u32 last_opcode = echos[echo_index].opcode;
				if(result < 0){
					const char *opstring = (last_opcode == IORING_OP_RECVMSG)
						? "IORING_OP_RECVMSG" : "IORING_OP_SENDMSG";
					printf("%s failed with error %d\n", opstring, result);
					echos[echo_index].opcode = IORING_OP_RECVMSG;
					echos[echo_index].iov.iov_len = sizeof(echos[i].buf);
				}else{
					if(last_opcode == IORING_OP_RECVMSG){
						printf("< \"%.*s\"\n", result, echos[echo_index].buf);
						echos[echo_index].opcode = IORING_OP_SENDMSG;
						echos[echo_index].iov.iov_len = result;
					}else{
						echos[echo_index].opcode = IORING_OP_RECVMSG;
						echos[echo_index].iov.iov_len = sizeof(echos[i].buf);
					}

				}
				echo_sq[echo_sq_num] = echo_index;
				echo_sq_num += 1;
			}
			__atomic_store_n(cq_head, tail, __ATOMIC_RELEASE);
		}

		// NOTE: push submissions
		head = __atomic_load_n(sq_head, __ATOMIC_ACQUIRE);
		tail = *sq_tail;
		mask = *sq_ring_mask;
		num_entries = tail - head;
		if(echo_sq_num > 0 && num_entries < sq_entries){
			u32 free_entries = sq_entries - num_entries;
			u32 to_submit = echo_sq_num;
			if(to_submit > free_entries)
				to_submit = free_entries;

			for(u32 i = 0; i < to_submit; i += 1){
				u32 sqe_index = (tail + i) & mask;
				u32 echo_index = echo_sq[i];
				memset(&sqes[sqe_index], 0, sizeof(struct io_uring_sqe));
				sqes[sqe_index].opcode = echos[echo_index].opcode;
				sqes[sqe_index].fd = s;
				sqes[sqe_index].user_data = echo_index;
				sqes[sqe_index].addr = (u64)&echos[echo_index].msg;
				sq_array[sqe_index] = sqe_index;
				printf("sqe %d: user_data = %llu\n",
					sqe_index, sqes[sqe_index].user_data);
			}

			__atomic_store_n(sq_tail, (tail + to_submit), __ATOMIC_RELEASE);

#if USE_SQPOLL
			u32 flags = __atomic_load_n(sq_flags, __ATOMIC_RELAXED);
			if(flags & IORING_SQ_NEED_WAKEUP){
				printf("IORING_SQ_NEED_WAKEUP\n");
				int ret = io_uring_enter(ring_fd, 0, 0, IORING_ENTER_SQ_WAKEUP);
				if(ret < 0)
					printf("io_uring_enter failed (errno = %d)\n", errno);
			}
#else
			int ret = io_uring_enter(ring_fd, to_submit, 0, 0);
			if(ret < 0)
				printf("io_uring_enter failed (errno = %d)\n", errno);
#endif

			if(to_submit != echo_sq_num){
				u32 sleft = echo_sq_num - to_submit;
				for(u32 i = 0; i < sleft; i += 1)
					echo_sq[i] = echo_sq[to_submit + i];
				echo_sq_num -= to_submit;
			}else{
				echo_sq_num = 0;
			}
		}

		ms_sleep(50);
	}

	// NOTE: We are not cleaning anything here because the OS will reclaim all resources used, etc.
	return 0;
}

