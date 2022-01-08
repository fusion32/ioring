#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>

#define PORT 7171

int main(int argc, char **argv){
	printf("sizeof(struct sockaddr): %zu\n", sizeof(struct sockaddr));
	printf("sizeof(struct sockaddr_in): %zu\n", sizeof(struct sockaddr_in));
//	printf("sizeof(struct sockaddr_un): %zu\n", sizeof(struct sockaddr_un));
	printf("sizeof(struct sockaddr_in6): %zu\n", sizeof(struct sockaddr_in6));
	printf("sizeof(struct sockaddr_storage): %zu\n", sizeof(struct sockaddr_storage));

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
		// NOTE: I think SO_LINGER only has a purpose for TCP connections.
		// I should look more into it.
		struct linger linger;
		linger.l_onoff = 0;
		linger.l_linger = 0;
		int ret = setsockopt(s, SOL_SOCKET, SO_LINGER, &linger, sizeof(struct linger));
		if(ret == -1){
			printf("failed to set socket option SO_LINGER (errno = %d)\n", errno);
			return -1;
		}
	}

	{
		struct sockaddr_in addr;
		addr.sin_family = AF_INET;
		addr.sin_port = htons(PORT);
		addr.sin_addr.s_addr = htonl(INADDR_ANY);
		int ret = bind(s, (struct sockaddr*)&addr, sizeof(struct sockaddr_in));
		if(ret == -1){
			printf("failed to bind socket to port %d (errno = %d)\n", PORT, errno);
			return -1;
		}
	}

	printf("online...\n");
	while(1){
		char buf[2048];

		struct sockaddr_in client;
		struct iovec iov;
		iov.iov_base = buf;
		iov.iov_len = sizeof(buf);

		struct msghdr mhdr;
		mhdr.msg_name = &client;
		mhdr.msg_namelen = sizeof(struct sockaddr_in);
		mhdr.msg_iov = &iov;
		mhdr.msg_iovlen = 1;
		mhdr.msg_control = NULL;
		mhdr.msg_controllen = 0;
		mhdr.msg_flags = 0;
		int ret = recvmsg(s, &mhdr, 0);
		if(ret > 0){
			printf("< %.*s\n", ret, buf);
			if(strncmp("quit", buf, 4) == 0){
				printf("quitting...\n");
				break;
			}

			iov.iov_len = ret;
			ret = sendmsg(s, &mhdr, 0);
			if(ret == -1){
				printf("failed to send message (errno = %d)\n", errno);
			}
		}else{
			printf("failed to recv message (errno = %d)\n", errno);
		}
	}
	return 0;
}

