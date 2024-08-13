#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <pthread.h>
#include <openssl/sha.h>

#define FD 4

// 사용할 전역변수 선언
int difficulty_bits;
int cnt = 0, printFlag = 0;
pthread_t wrkrId[5];
pthread_mutex_t mutex; // mutex 객체 선언

// thread에 전달할 구조체 정의
typedef struct threadArgs {
	int wrkrSd;
	int no;
} threadArgs;

// start_routine 메서드 정의
void* wrkrThread(void* args) {
	// 지역 변수 선언
	int nSent, nRecv;
	char challenge_nonce[50], tmp[30];
	char sBuff[BUFSIZ], rBuff[BUFSIZ];
	int nonce, i;
	threadArgs* thData = (threadArgs*)args;
	const char* challenge = "김희환||염한울||조석현";
	double totalTime;

	// 연결 상태 출력
	printf("%d번 머신에 연결되었습니다.\n", thData->wrkrSd - 3);

	// 워킹 서버로 챌린지 보내기
	sprintf(sBuff, "%d%d%s", thData->wrkrSd, difficulty_bits, challenge);
	nSent = send(thData->wrkrSd, sBuff, strlen(sBuff), 0);
	if(nSent == -1) {
		perror("Writing to Work Server Failed!");
		close(thData->wrkrSd);
		pthread_exit(NULL);
	}
	printf("챌린지: %s 전송 완료!\n", sBuff + 2);

	// 워킹 서버로부터 결과 수신
	nRecv = recv(thData->wrkrSd, rBuff, sizeof(rBuff), 0);
	if(nRecv == -1) {
		perror("Receving challenge failed!");
		pthread_exit(NULL);
	}

	// critical section 보호 설정
	pthread_mutex_lock(&mutex);
	printFlag++;
	pthread_mutex_unlock(&mutex);
	
	// 가장 먼저 성공한 thread만 출력기능 가짐
	if(printFlag == 1) {
		rBuff[nRecv] = '\0';
		for(i = 0; i < 10; i++)
			tmp[i] = rBuff[i];
		totalTime = atof(tmp);
		for(i = 0; i < 10; i++)
			tmp[i] = rBuff[i + 10];
		nonce = atoi(tmp);
		sprintf(tmp, "%s", rBuff + 20);
		printf("--------------------------------------------------\n");
		printf("Valid Nonce: %d\n", nonce);
		printf("Result: %s\n", tmp);
		printf("CPU used time: 약 %.1f초\n", totalTime);

		// 성공 메시지를 모든 worker들에게 전송
		sprintf(sBuff, "%d %s -> Checked!", nonce, tmp);
		for(i = 0; i < cnt; i++) {	
			if (send(FD + i, sBuff, strlen(sBuff), 0) == -1) {
				perror("Sending result to Work Server failed!");				
				close(thData->wrkrSd);		
				pthread_exit(NULL);
			}
		}
		printf("워킹 서버에 알림 완료!\n");
	}

	// 나머지 thread들 종료
	for(i = 0; i < cnt; i++) {
		if(thData->no == i) continue;
		pthread_cancel(wrkrId[i]);
	}

	// 현재 thread 종료
	pthread_exit(NULL);
}

int main(int argc, char* argv[])
{
	// 사용할 변수 선언
	int srvSd, clntSd;
	struct sockaddr_in srvAddr, clntAddr;
	socklen_t clntAddrLen;
	threadArgs* args = (threadArgs*)malloc(sizeof(threadArgs));
	int i;
	
	// mutex 초기화
	pthread_mutex_init(&mutex, NULL);

	// 예외 처리
	if (argc != 4) {
		perror("Usage: [Filename] [Port] [MACHINE_COUNT] [Difficulty_Bits]");
		return EXIT_FAILURE;
	}
	difficulty_bits = atoi(argv[3]);

	// 소켓 생성
	srvSd = socket(AF_INET, SOCK_STREAM, 0);
	if (srvSd == -1) {
		perror("Socket creation failed!");
		return EXIT_FAILURE;
	}
	
	// 소켓 bind
	srvAddr.sin_family = AF_INET;
	srvAddr.sin_addr.s_addr = htonl(INADDR_ANY);
	srvAddr.sin_port = htons(atoi(argv[1]));
	if (bind(srvSd, (struct sockaddr*)&srvAddr, sizeof(srvAddr)) == -1) {
		perror("Socket binding failed!");
		close(srvSd);
		return EXIT_FAILURE;
	}
	
	// listen 상태로 전환
	if (listen(srvSd, 3) == -1) { // 최대 worker = 3
		perror("Listening on socket failed!");
		close(srvSd);
		return EXIT_FAILURE;
	}
	printf("연결을 기다리는 중 . . .\n");
	
	// worker의 연결 요청 수락
	clntAddrLen = sizeof(clntAddr);
	while (1) {
		// Accept 메서드 호출
		clntSd = accept(srvSd, (struct sockaddr*)&clntAddr, &clntAddrLen);
		if (clntSd == -1) {
			perror("Accepting client connection failed!");
			close(srvSd);
			return EXIT_FAILURE;
		}
		
		// worker 관련 thread 생성
		// 전달할 인자 초기화
		args->wrkrSd = clntSd;
		args->no = cnt;
		if (pthread_create(&wrkrId[cnt], NULL, wrkrThread, (void*)args) != 0) {
			perror("Creating worker thread failed!");
			close(clntSd);
			close(srvSd);
			return EXIT_FAILURE;
		}

		// 가장 마지막에 참여한 머신 thread에 맞게 대기
		// 입력된 arg만큼의 머신이 반드시 참여!
		cnt++;
		if(cnt < atoi(argv[2]))
			pthread_detach(wrkrId[cnt - 1]);
		else {
			pthread_join(wrkrId[cnt - 1], NULL);
			break;
		}		
	}

	// 소켓 닫기
	for(i = 0; i < cnt; i++)
		close(FD + i);
	close(srvSd);
	pthread_mutex_destroy(&mutex);
	return 0;
}

