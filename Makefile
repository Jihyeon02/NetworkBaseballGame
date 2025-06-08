# NeNetGame/Makefile
CC = gcc
CFLAGS = -Wall -Wextra -std=c99 -g -I/opt/homebrew/include
LIBS = -L/opt/homebrew/lib -ljson-c
PTHREAD_LIBS = -L/opt/homebrew/lib -ljson-c -lpthread

# 타겟 실행 파일
SERVER = baseball_server
CLIENT = baseball_client
PERF_TEST = performance_test
CONN_TEST = connection_test

# 소스 파일
SERVER_SRC = baseball_server.c
CLIENT_SRC = baseball_client.c
PERF_TEST_SRC = performance_test.c
CONN_TEST_SRC = connection_test.c
PROTOCOL_H = baseball_protocol.h

# 기본 타겟
all: $(SERVER) $(CLIENT) $(PERF_TEST) $(CONN_TEST)

# 서버 컴파일
$(SERVER): $(SERVER_SRC) $(PROTOCOL_H)
	$(CC) $(CFLAGS) -o $(SERVER) $(SERVER_SRC) $(LIBS)

# 클라이언트 컴파일
$(CLIENT): $(CLIENT_SRC) $(PROTOCOL_H)
	$(CC) $(CFLAGS) -o $(CLIENT) $(CLIENT_SRC) $(LIBS)

# 성능 테스트 컴파일
$(PERF_TEST): $(PERF_TEST_SRC)
	$(CC) $(CFLAGS) -o $(PERF_TEST) $(PERF_TEST_SRC) $(PTHREAD_LIBS)

# 연결 테스트 컴파일
$(CONN_TEST): $(CONN_TEST_SRC)
	$(CC) $(CFLAGS) -o $(CONN_TEST) $(CONN_TEST_SRC)

# 테스트 실행
test: all
	@echo "=========================================="
	@echo "숫자 야구 네트워크 게임 테스트"
	@echo "=========================================="
	@echo "서버 실행: ./$(SERVER) 8080"
	@echo "클라이언트 실행: ./$(CLIENT) 127.0.0.1 8080"
	@echo "성능 테스트: ./$(PERF_TEST)"
	@echo "연결 테스트: ./$(CONN_TEST)"
	@echo "=========================================="

# 정리
clean:
	rm -f $(SERVER) $(CLIENT) $(PERF_TEST) $(CONN_TEST)

# 게임 실행 도우미
run-server:
	./$(SERVER) 8080

run-client:
	./$(CLIENT) 127.0.0.1 8080

# 성능 테스트 실행
run-performance: $(PERF_TEST)
	@echo "성능 테스트를 시작합니다..."
	./$(PERF_TEST)

run-json-test: $(PERF_TEST)
	@echo "JSON 성능만 테스트합니다"
	./$(PERF_TEST) --json-only

run-load-test: $(PERF_TEST)
	@echo "부하 테스트만 실행합니다 (서버가 실행 중이어야 함)"
	./$(PERF_TEST) --load-only

# 연결 테스트 실행
run-connection-test: $(CONN_TEST)
	@echo "연결 테스트를 시작합니다..."
	./$(CONN_TEST)

run-connection-monitor: $(CONN_TEST)
	@echo "연결 모니터링 테스트 (서버를 Ctrl+C로 종료해보세요)"
	./$(CONN_TEST) --monitor

run-error-test: $(CONN_TEST)
	@echo "네트워크 오류 시뮬레이션"
	./$(CONN_TEST) --errors

# json-c 라이브러리 설치 확인
check-deps:
	@echo "json-c 라이브러리 확인 중..."
	@pkg-config --exists json-c && echo "✅ json-c 설치됨" || echo "❌ json-c 미설치 - 설치 필요: brew install json-c"

.PHONY: all test clean run-server run-client run-performance run-json-test run-load-test run-connection-test run-connection-monitor run-error-test check-deps
