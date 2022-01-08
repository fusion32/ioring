#include <errno.h>
#include <stdio.h>
#include <string.h>
//#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>

#define PORT 7171

int main(int argc, char **argv){
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
		struct sockaddr sender;
		socklen_t sender_len;
		int ret = recvfrom(s, buf, sizeof(buf), 0, &sender, &sender_len);
		if(ret > 0){
			printf("< %.*s\n", ret, buf);
			if(strncmp("quit", buf, 4) == 0){
				printf("quitting...\n");
				break;
			}

			ret = sendto(s, buf, ret, 0, &sender, sender_len);
			if(ret <= 0){
				printf("failed to submit message\n");
			}
		}else{
			printf("failed to recv message\n");
		}
	}
	return 0;
}

