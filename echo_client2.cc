#include <errno.h>
#include <stdio.h>
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
	int s = socket(AF_INET, SOCK_DGRAM, 0);
	if(s == -1){
		printf("failed to create socket (errno = %d)\n", errno);
		return -1;
	}

	struct sockaddr_in server;
	server.sin_family = AF_INET;
	server.sin_port = htons(PORT);
	server.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	while(1){
		char buf[2048];
		printf("> ");
		readline(buf, sizeof(buf));
		
		struct iovec iov;
		iov.iov_base = buf;
		iov.iov_len = strlen(buf);

		struct msghdr mhdr;
		mhdr.msg_name = &server;
		mhdr.msg_namelen = sizeof(struct sockaddr_in);
		mhdr.msg_iov = &iov;
		mhdr.msg_iovlen = 1;
		mhdr.msg_control = NULL;
		mhdr.msg_controllen = 0;
		mhdr.msg_flags = 0;
		int ret = sendmsg(s, &mhdr, 0);
		if(ret != -1){
			if(strncmp("quit", buf, 4) == 0){
				printf("quitting...\n");
				break;
			}

			iov.iov_len = sizeof(buf);
			mhdr.msg_name = NULL;
			mhdr.msg_namelen = 0;
			ret = recvmsg(s, &mhdr, 0);
			if(ret != -1){
				printf("< \"%.*s\"\n", ret, buf);
			}else{
				printf("failed to receive message (errno = %d)\n", errno);
			}
		}else{
			printf("failed to send message (errno = %d)\n", errno);
		}
	}
	return 0;
}

