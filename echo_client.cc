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
		
		int ret = sendto(s, buf, strlen(buf), 0, (struct sockaddr*)&server, sizeof(struct sockaddr_in));
		if(ret > 0){
			if(strncmp("quit", buf, 4) == 0){
				printf("quitting...\n");
				break;
			}

			ret = recvfrom(s, buf, ret, 0, NULL, NULL);
			if(ret > 0){
				printf("< %.*s\n", ret, buf);
			}else{
				printf("failed to receive message\n");
			}
		}else{
			printf("failed to send message\n");
		}
	}
	return 0;
}

