#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <openssl/sha.h>
#include <time.h>

#define FD 4
#define nonceGap 700000000

// 사용할 전역변수 선언
int breakFlag = 0;

// start_routine 메서드 정의
void* start_routine(void* args) {
	// 지역 변수 선언
	int wrkrSd = *((int*)args);
	char buff[BUFSIZ];

	// 서버로부터 응답 수신
	if (recv(wrkrSd, buff, sizeof(buff), 0) == -1) {
		perror("Reading from Main Server failed!");
		close(wrkrSd);
		breakFlag = 1;
		pthread_exit(NULL);
	}
	// 수신 내용 출력
	printf("------------------------------------------------------------\n");
	printf("%s\n", buff);

	// thread 종료에 따른 flag 변화
	breakFlag = 1;
	pthread_exit(NULL);
}

// 해시 계산 메서드
void calculateHash(const char* input, char* output) {
	// 지역 변수 선언
	int i;
	unsigned char hash[SHA_DIGEST_LENGTH]; // 해시값을 담을 배열

	// 해시값을 계산해서 배열에 담기
	SHA1((unsigned char*)input, strlen(input), hash);

	// 배열을 버퍼로 출력
	for (i = 0; i < SHA_DIGEST_LENGTH; i++)
		sprintf(output + (i * 2), "%02x", hash[i]);
}

int main(int argc, char* argv[])
{
	// PoW 타이머 설정
	clock_t start, end;
	double totalTime;

	// 변수 선언
	int wrkrSd, nRecv;
	struct sockaddr_in mainAddr;
	char challenge[50], challenge_nonce[50];
	int machineNo, difficulty_bits, nonce, i;
	int	isValid = 0; // 올바른 해시 값인지에 대한 Flag
	char res[SHA_DIGEST_LENGTH * 2 + 1], buff[BUFSIZ];
	pthread_t thread;

	// 예외 처리
	if(argc != 3) {
		perror("Usage: [Filename] [IP Address] [Port]");
		return EXIT_FAILURE;
	}

	// 소켓 생성
	wrkrSd = socket(AF_INET, SOCK_STREAM, 0);
	if (wrkrSd == -1) {
		perror("Socket creation failed!");
		return EXIT_FAILURE;
	}

	// 소켓 주소 구조체 초기화
	mainAddr.sin_family = AF_INET;
	mainAddr.sin_addr.s_addr = inet_addr(argv[1]);
	mainAddr.sin_port = htons(atoi(argv[2]));

	// 메인서버에 연결 요청
	if (connect(wrkrSd, (struct sockaddr*)&mainAddr, sizeof(mainAddr)) == -1) {
		perror("Connection to Main Server failed!");
		close(wrkrSd);
		return EXIT_FAILURE;
	}
	printf("메인 서버와 연결 완료~\n");
    
	// 메인 서버로부터 챌린지 수신
	nRecv = recv(wrkrSd, challenge, sizeof(challenge), 0);
	if(nRecv == -1) {
		perror("Receiving Challenge from Main Server failed!");
		close(wrkrSd);
		return EXIT_FAILURE;
	}
	machineNo = challenge[0] - '0';
	difficulty_bits = challenge[1] - '0';
	challenge[nRecv] = '\0'; // 널 문자 추가
	printf("메인 서버로부터 챌린지 수신: %s\n", challenge + 2);
	
	// 듣기 thread 생성
	if (pthread_create(&thread, NULL, start_routine, (void*)&wrkrSd) != 0) {
		perror("Thread creation failed!");
		close(wrkrSd);
		return EXIT_FAILURE;
	}
		
	// PoW 계산
	nonce = 380000000;
	start = clock(); // 타이머 시작
	while (!isValid) {
		// breakFlag 분기
		if(breakFlag) {
			// 소켓 닫고 프로그램 종료
			printf("결과가 도출되었으므로 프로그램 종료!\n");
			close(wrkrSd);
			return 0;
		}

		// challenge + nonce값 병합
		sprintf(challenge_nonce, "%s%08x", challenge + 2, nonce);
		// 병합된 값으로 해시 값 계산
		calculateHash(challenge_nonce, res);

		// 난이도 조건을 충족하는지 테스트
		isValid = 1;
		for (i = 0; i < difficulty_bits; i++) {
			// 미충족시 flag 변경후 탈출
			if (res[i] != '0') {
				isValid = 0;
				break;
			}
		}

		nonce++; // 매 반복마다 nonce값 1씩 증가
		// 10만 단위로 nonce 출력
		if(nonce % 100000 == 0)
			printf("nonce -> %d\n", nonce);
	}
	end = clock(); // 타이머 종료
	totalTime = ((double)(end - start)) / CLOCKS_PER_SEC;

	// 성공한 nonce 및 해시 값 출력
	printf("-----------------------------------------------------------\n");
	printf("Valid Nonce: %d\n", nonce);
	printf("Result: %s\n", res);

	// 메인 서버로 타이머, nonce 및 해시 값 전송
	sprintf(buff, "%010.1f%010d%s", totalTime, nonce, res);
	if(send(wrkrSd, buff, strlen(buff), 0) == -1) {
		perror("Sedning Result to Main Server failed!");
		return EXIT_FAILURE;
	}
	printf("메인 서버로 결과 전송 완료!\n");

	// 전송한 해시 값 확인받을 때까지 대기
	if(pthread_join(thread, NULL) != 0) {
		perror("Joining the Thread failed!");
		close(wrkrSd);
		return EXIT_FAILURE;
	}

	// 소켓 닫기
	close(wrkrSd);
	return 0;
}

