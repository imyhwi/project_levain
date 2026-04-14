/* levain_sql_client.c */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <pthread.h>
#include <mysql/mysql.h>
#include <time.h>

#define BUF_SIZE 256
#define NAME_SIZE 20
#define MAX_CLNT 32
#define CONTAINER_DEPTH 90.0 - 20.0
#define SAVE_INTERVAL 300 // DB 저장 주기 = 60초 x 5 = 5분 

// DB 설정
#define DB_HOST "localhost"
#define DB_USER "pi"
#define DB_PASS "raspberry"
#define DB_NAME "levain_project"

typedef struct {
    char device_id[10];
    time_t start_time;					// 기기별 가동 시작 시간
} PROCESS_INFO;
PROCESS_INFO process_map[MAX_CLNT];		// 기기별 가동 시작 시간 저장소

char my_id[NAME_SIZE];
MYSQL *conn;							// DB 연결 객체

void *recv_msg(void *arg);
void manage_process_time(char *dev_id, char *state, char *output_buf);
void getlocaltime(char *buf);
void error_handling(char *msg);

// DB 관련 함수 선언
void connect_db();
void close_db();
void process_sensor_data(char *raw_msg);


int main(int argc, char *argv[])
{
    int sock;
    struct sockaddr_in serv_addr;
    pthread_t rcv_thread;
    char msg[BUF_SIZE];

    // 인자 4개 (실행파일 IP PORT ID)
    if (argc != 4) {
        printf("Usage : %s <IP> <PORT> <ID>\n", argv[0]); 
        exit(1);
    }

    strcpy(my_id, argv[3]); // 입력받은 ID 사용

    // 1. DB 연결
    connect_db();
    memset(process_map, 0, sizeof(process_map));

    // 2. 소켓 연결
    sock = socket(PF_INET, SOCK_STREAM, 0);
    if (sock == -1) error_handling("socket() error");

    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = inet_addr(argv[1]);
    serv_addr.sin_port = htons(atoi(argv[2]));

    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) == -1)
        error_handling("connect() error");

    // 3. 로그인 (프로토콜: [ID:PASSWD])
    // 주의: 서버의 idpasswd.txt에 해당 ID와 PASSWD가 등록되어 있어야 함
    sprintf(msg, "[%s:PASSWD]", my_id);
    write(sock, msg, strlen(msg));

    printf(">>> SQL Client (%s) Started. Tracking Process...\n", my_id);

    // 4. 수신 스레드 시작
    pthread_create(&rcv_thread, NULL, recv_msg, (void *)&sock);
    pthread_join(rcv_thread, NULL);

    close_db();
    close(sock);
    return 0;
}

void *recv_msg(void *arg)
{
    int sock = *((int *)arg);
    char name_msg[BUF_SIZE];
    int str_len;

    while (1) {
        str_len = read(sock, name_msg, BUF_SIZE - 1);
        if (str_len <= 0) {
            printf("Server Disconnected.\n");
            break;
        }
        name_msg[str_len] = 0;

        // [ALLMSG]가 포함된 메시지만 처리
        char *start_ptr = strstr(name_msg, "[ALLMSG]");
        if (start_ptr != NULL) {
            process_sensor_data(start_ptr + 8); // 태그 떼고 보냄
        }
    }
    return NULL;
}

void process_sensor_data(char *raw_msg)
{
    char dev_id[20] = "Unknown";
    char mode[10] = "-", state[10] = "-";
    char time_str[30], start_time[30] = "NULL", run_time[20] = "-";
    float temp = 0.0, hum = 0.0;
    int dist = 0, co2 = 0, heater = 0, cooler = 0, humidifier = 0;
    
    // 저장 주기 관리 변수
    static time_t last_save = 0;
    static char last_state[10] = "STOP";
    time_t now = time(NULL);

    getlocaltime(time_str);

    // 파싱
    char buf[BUF_SIZE];
    strcpy(buf, raw_msg);
    char *token = strtok(buf, "|");
    while (token != NULL) {
        if (strncmp(token, "ID:", 3) == 0) strcpy(dev_id, token + 3);
        else if (strncmp(token, "MODE:", 5) == 0) strcpy(mode, token + 5);
        else if (strncmp(token, "STATE:", 6) == 0) strcpy(state, token + 6);
        else if (strncmp(token, "TIME:", 5) == 0) {
            int mins = atoi(token + 5);
            sprintf(run_time, "%02dd %02dh %02dm", mins / 1440, (mins % 1440) / 60, mins % 60);
        }
        else if (strncmp(token, "T:", 2) == 0) temp = atof(token + 2);
        else if (strncmp(token, "H:", 2) == 0) hum = atof(token + 2);
        else if (strncmp(token, "D:", 2) == 0) dist = atof(token + 2);
        else if (strncmp(token, "CO2:", 4) == 0) co2 = atof(token + 4);
        else if (strncmp(token, "HEAT:", 5) == 0) heater = (strstr(token, "ON") ? 1 : 0);
        else if (strncmp(token, "COOL:", 5) == 0) cooler = (strstr(token, "ON") ? 1 : 0);
        else if (strncmp(token, "HUM:", 4) == 0) humidifier = (strstr(token, "ON") ? 1 : 0);
        token = strtok(NULL, "|");
    }

    // 1. 필터링 (STM, 쓰레기값)
    if (strstr(dev_id, "STM") != NULL) return;
    if (temp != temp) temp = 0.0;
    if (hum != hum) hum = 0.0;
    if (temp == 0.0 && hum == 0.0) return;    // 유효하지 않은 값 무시 (둘 다 0)
    // 2. 반죽 높이 계산
    float dough_height = CONTAINER_DEPTH - dist;
    if (dough_height < 0) dough_height = 0;

    // 3. 저장 여부 판단 (주기 or 상태변경)
    int state_changed = (strcmp(state, last_state) != 0);
    if (!state_changed && (now - last_save < SAVE_INTERVAL)) return;

    // 4. 공정 시간 관리
    manage_process_time(dev_id, state, start_time);

    // 5. DB 저장
    char query[1024];
    if (strcmp(start_time, "NULL") == 0) {
        sprintf(query, "INSERT INTO levain_logs "
            "(device_id, mode, state, run_time, started_at, log_time, temp, humidity, dough_height, co2, heater, cooler, humidifier) "
            "VALUES ('%s', '%s', '%s', '%s', NULL, '%s', %.2f, %.2f, %.2f, %.2f, %d, %d, %d)",
            dev_id, mode, state, run_time, time_str, temp, hum, dough_height, co2, heater, cooler, humidifier);
    } else {
        sprintf(query, "INSERT INTO levain_logs "
            "(device_id, mode, state, run_time, started_at, log_time, temp, humidity, dough_height, co2, heater, cooler, humidifier) "
            "VALUES ('%s', '%s', '%s', '%s', '%s', '%s', %.2f, %.2f, %.2f, %.2f, %d, %d, %d)",
            dev_id, mode, state, run_time, start_time, time_str, temp, hum, dough_height, co2, heater, cooler, humidifier);
    }

    if (mysql_query(conn, query)) {
        fprintf(stderr, "DB Error: %s\n", mysql_error(conn));
        if(mysql_ping(conn)) connect_db();
    } else {
        printf("[%s] DB Saved!", time_str);
        last_save = now;
        strcpy(last_state, state);
    }
}

void manage_process_time(char *dev_id, char *state, char *output_buf) {
    int idx = -1;
    for(int i=0; i<MAX_CLNT; i++) {
        if(strcmp(process_map[i].device_id, dev_id) == 0) { idx = i; break; }
    }
    if(idx == -1) {
        for(int i=0; i<MAX_CLNT; i++) {
            if(process_map[i].device_id[0] == '\0') {
                strcpy(process_map[i].device_id, dev_id); process_map[i].start_time = 0; idx = i; break;
            }
        }
    }
    if(idx == -1) { strcpy(output_buf, "NULL"); return; }

    // RUN일 때만 시간 기록 (기존 시간 없으면 현재시간, 있으면 유지)
    if (strstr(state, "RUN")) {
        if (process_map[idx].start_time == 0) process_map[idx].start_time = time(NULL);
    } 
    // STOP/END면 시간 초기화
    else if (strstr(state, "STOP") || strstr(state, "END")) {
        process_map[idx].start_time = 0;
    }
    // PAUSE는 아무것도 안 함 (기존 시간 유지)

    if (process_map[idx].start_time != 0) {
        struct tm *t = localtime(&process_map[idx].start_time);
        sprintf(output_buf, "%04d-%02d-%02d %02d:%02d:%02d", 
                t->tm_year+1900, t->tm_mon+1, t->tm_mday, t->tm_hour, t->tm_min, t->tm_sec);
    } else {
        strcpy(output_buf, "NULL");
    }
}

void connect_db() {
    conn = mysql_init(NULL);
    if (!mysql_real_connect(conn, DB_HOST, DB_USER, DB_PASS, DB_NAME, 0, NULL, 0)) {
        fprintf(stderr, "DB Connect Error: %s\n", mysql_error(conn)); exit(1);
    }
}

void close_db() {
    if (conn != NULL) {
        mysql_close(conn);
        conn = NULL;
        printf(">>> DB Disconnected.\n");
    }
}

void getlocaltime(char *buf) {
    struct tm *t; time_t tt = time(NULL); t = localtime(&tt);
    sprintf(buf, "%04d-%02d-%02d %02d:%02d:%02d", 
        t->tm_year+1900, t->tm_mon+1, t->tm_mday, t->tm_hour, t->tm_min, t->tm_sec);
}

void error_handling(char *msg) {
    fputs(msg, stderr); fputc('\n', stderr); exit(1);
}
