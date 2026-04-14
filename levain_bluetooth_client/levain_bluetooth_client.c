#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <pthread.h>
#include <signal.h>
#include <bluetooth/bluetooth.h>
#include <bluetooth/rfcomm.h>

#define BUF_SIZE 512  // [수정] 데이터 길이를 고려해 100 -> 512로 증가
#define NAME_SIZE 20
#define ARR_CNT 5

void * send_msg(void * arg);
void * recv_msg(void * arg);
void error_handling(char * msg);

char name[NAME_SIZE];

typedef struct {
	int sockfd;	
	int btfd;	
	char sendid[NAME_SIZE];
} DEV_FD;

int main(int argc, char *argv[])
{
	DEV_FD dev_fd;
	struct sockaddr_in serv_addr;
	pthread_t snd_thread, rcv_thread;
	void * thread_return;
	int ret;
	
    // 블루투스 설정
	struct sockaddr_rc addr = { 0 };
    // [중요] 본인의 HC-06 MAC 주소로 변경해야 합니다.
  	char dest[18] = "98:DA:60:09:6E:F4";
	char msg[BUF_SIZE];

	if(argc != 4) {
		printf("Usage : %s <IP> <port> <ID>\n", argv[0]);
		exit(1);
	}

	sprintf(name, "%s", argv[3]);

    // 1. TCP 소켓 생성 (서버 연결용)
	dev_fd.sockfd = socket(PF_INET, SOCK_STREAM, 0);
	if(dev_fd.sockfd == -1)
		error_handling("socket() error");

	memset(&serv_addr, 0, sizeof(serv_addr));
	serv_addr.sin_family = AF_INET;
	serv_addr.sin_addr.s_addr = inet_addr(argv[1]);
	serv_addr.sin_port = htons(atoi(argv[2]));

	if(connect(dev_fd.sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) == -1)
		error_handling("connect() error");

    // 서버 로그인
	sprintf(msg,"[%s:PASSWD]", name);
	write(dev_fd.sockfd, msg, strlen(msg));
    printf(">>> Connected to IoT Server as %s\n", name);

    // 2. 블루투스 소켓 생성 (아두이노 연결용)
	dev_fd.btfd = socket(AF_BLUETOOTH, SOCK_STREAM, BTPROTO_RFCOMM);
	if(dev_fd.btfd == -1){
		perror("socket()");
		exit(1);
	}

	addr.rc_family = AF_BLUETOOTH;
	addr.rc_channel = (uint8_t)1;
	str2ba(dest, &addr.rc_bdaddr);

    printf(">>> Connecting to Arduino Bluetooth (%s)...\n", dest);
	ret = connect(dev_fd.btfd, (struct sockaddr *)&addr, sizeof(addr));
	if(ret == -1){
		perror("bluetooth connect()");
        // 블루투스 연결 실패해도 프로그램이 죽지 않도록 처리 (선택사항)
		exit(1); 
	}
    printf(">>> Bluetooth Connected!\n");

	pthread_create(&rcv_thread, NULL, recv_msg, (void *)&dev_fd);
	pthread_create(&snd_thread, NULL, send_msg, (void *)&dev_fd);

	pthread_join(snd_thread, &thread_return);
    // pthread_join(rcv_thread, &thread_return); // 수신 스레드는 detach하거나 메인 종료시 함께 종료

	close(dev_fd.sockfd);
    close(dev_fd.btfd);
	return 0;
}

// 아두이노 -> 서버 전송 (send_msg)
void * send_msg(void * arg)
{
	DEV_FD *dev_fd = (DEV_FD *)arg;
	int ret;
	fd_set initset, newset;
	struct timeval tv;
	char name_msg[BUF_SIZE * 2]; // 넉넉하게
	char msg[BUF_SIZE];
	int total=0;

	FD_ZERO(&initset);
	FD_SET(dev_fd->btfd, &initset);

	while(1) {
		tv.tv_sec = 1;
		tv.tv_usec = 0;
		newset = initset;
		
        // 블루투스에서 데이터가 오는지 감시
		ret = select(dev_fd->btfd + 1, &newset, NULL, NULL, &tv);
		
		if(FD_ISSET(dev_fd->btfd, &newset))
		{
            // 버퍼 오버플로우 방지
            if (total >= BUF_SIZE - 1) total = 0;

			ret = read(dev_fd->btfd, msg + total, BUF_SIZE - total - 1);
			if(ret > 0)
			{
				total += ret;
                msg[total] = 0; // null terminate
			}
			else if(ret == 0) {
                printf("Bluetooth Disconnected.\n");
				dev_fd->sockfd = -1;
				return NULL;
			}

            // 개행문자('\n')를 만나면 메시지 완성으로 간주하고 전송
			if(strchr(msg, '\n') != NULL)
			{
                // [수정 핵심] 서버 라우팅을 위해 [ALLMSG]를 앞에 붙여줍니다.
                // 아두이노가 보낸 데이터: "[ALLMSG]ID:LEV_MEGA|..."
                // 서버로 보내는 데이터: "[ALLMSG][ALLMSG]ID:LEV_MEGA|..."
                // 서버 처리 후: "[LEV_BT][ALLMSG]ID:LEV_MEGA|..." (이렇게 되어야 SQL Client가 인식함)
                
				sprintf(name_msg, "[ALLMSG]%s", msg); 
                
  				fputs(name_msg, stdout); // 디버깅용 출력
				if(write(dev_fd->sockfd, name_msg, strlen(name_msg)) <= 0)
				{
					dev_fd->sockfd = -1;
					return NULL;
				}
                
                // 전송 후 버퍼 초기화
				memset(msg, 0, sizeof(msg));
				total = 0;
			}
		}
		
        // 서버 소켓이 끊겼는지 확인
        if(dev_fd->sockfd == -1) return NULL;
	}
}

// 서버 -> 아두이노 전송 (recv_msg)
void * recv_msg(void * arg)
{
	DEV_FD *dev_fd = (DEV_FD *)arg;
	char name_msg[BUF_SIZE];
	int str_len;

	while(1) {
		memset(name_msg, 0x0, sizeof(name_msg));
		str_len = read(dev_fd->sockfd, name_msg, BUF_SIZE - 1);
		if(str_len <= 0) 
		{
            printf("Server Disconnected.\n");
			dev_fd->sockfd = -1;
			return NULL;
		}
		name_msg[str_len] = 0;
        
  		fputs("CMD from Server:", stdout);
		fputs(name_msg, stdout);

        // 서버에서 온 메시지(예: "[ANDROID]RUN")를 그대로 아두이노로 전달
        // 아두이노 코드는 "RUN", "PAUSE" 같은 문자열을 포함하는지 검사하므로 그대로 보내도 됨
		write(dev_fd->btfd, name_msg, strlen(name_msg));   
	}
}

void error_handling(char * msg)
{
	fputs(msg, stderr);
	fputc('\n', stderr);
	exit(1);
}
