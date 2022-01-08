#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>

#define PORT 7171

static
void readline(char *buf, int buflen){
	int writepos = 0;
	while(1){
		int c = getchar();
		if(c == EOF || c == '\n')
			break;

		// write to buf as long as there is room
		if(writepos < buflen){
			buf[writepos] = (char)c;
			writepos += 1;
		}
	}

	// write nul terminator
	if(writepos >= buflen)
		writepos = buflen - 1;
	buf[writepos] = 0;
}

int main(int argc, char **argv){
	int num_messages = 1000;
	if(argc < 2){
		printf("setting 'num_messages' to its default value %d\n", num_messages);
	}else{
		num_messages = atoi(argv[1]);
		printf("setting 'num_messages' to %d\n", num_messages);
	}

	int num_messages_per_batch = 200;
	if(argc < 3){
		printf("setting 'num_messages_per_batch' to its default value %d\n", num_messages_per_batch);
	}else{
		num_messages_per_batch = atoi(argv[2]);
		printf("setting 'num_messages_per_batch' to %d\n", num_messages_per_batch);
	}

	int s = socket(AF_INET, SOCK_DGRAM, 0);
	if(s == -1){
		printf("failed to create socket (errno = %d)\n", errno);
		return -1;
	}

	{
		int ret;
		struct timeval timeout;
		timeout.tv_sec = 1;
		timeout.tv_usec = 0;

		ret = setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(struct timeval));
		if(ret == -1){
			printf("failed to set socket option SO_RCVTIMEO (errno = %d)\n", errno);
			return -1;
		}

		ret = setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(struct timeval));
		if(ret == -1){
			printf("failed to set socket option SO_SNDTIMEO (errno = %d)\n", errno);
			return -1;
		}
	}

	struct sockaddr_in server;
	server.sin_family = AF_INET;
	server.sin_port = htons(PORT);
	server.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	while(1){
		char send_buf[2048];
		char recv_buf[2048];
		printf("> ");
		readline(send_buf, sizeof(send_buf));
		
		struct iovec iov;
		struct msghdr mhdr;
		mhdr.msg_iov = &iov;
		mhdr.msg_iovlen = 1;
		mhdr.msg_control = NULL;
		mhdr.msg_controllen = 0;
		mhdr.msg_flags = 0;

		// NOTE: We cannot send all messages and then recv all messages at once.
		// We start to lose all the messages after ~300. This is probably a kernel
		// limitation but is also unreasonable to do it. We should instead send
		// messages in batches of 100 or 200.

		int num_sent = 0;
		int num_recvd = 0;
		int num_recvd_correctly = 0;
		int num_batches = num_messages / num_messages_per_batch;
		for(int cur_batch = 0;
				cur_batch < num_batches;
				cur_batch += 1){
			int batch_sent = 0;
			int batch_recvd = 0;
			int batch_recvd_correctly = 0;

			// send message 'num_messages_per_batch' times
			iov.iov_base = send_buf;
			iov.iov_len = strlen(send_buf);
			mhdr.msg_name = &server;
			mhdr.msg_namelen = sizeof(struct sockaddr_in);
			for(int i = 0; i < num_messages_per_batch; i += 1){
				int ret = sendmsg(s, &mhdr, 0);
				if(ret != -1){
					batch_sent += 1;
				}else{
					printf("failed to send message (batch = %d, num = %d) (errno = %d)\n",
						cur_batch, i, errno);
				}
			}

			// read it back 'batch_sent' times
			iov.iov_base = recv_buf;
			iov.iov_len = sizeof(recv_buf);
			mhdr.msg_name = NULL;
			mhdr.msg_namelen = 0;
			for(int i = 0; i < batch_sent; i += 1){
				int ret = recvmsg(s, &mhdr, 0);
				if(ret != -1){
					batch_recvd += 1;
					if(strncmp(send_buf, recv_buf, ret) == 0)
						batch_recvd_correctly += 1;
				}else{
					printf("failed to receive message (batch = %d, num = %d) (errno = %d)\n",
						cur_batch, i, errno);
				}
			}

			// add results from this batch
			num_sent += batch_sent;
			num_recvd += batch_recvd;
			num_recvd_correctly += batch_recvd_correctly;
		}
		printf("num_sent = %d, num_recvd = %d, num_recvd_correctly = %d\n",
			num_sent, num_recvd, num_recvd_correctly);
	}
	return 0;
}

