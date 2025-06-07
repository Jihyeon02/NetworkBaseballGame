# NeNetGame/Makefile
CC = gcc
CFLAGS = -Wall -Wextra -std=c99 -g -I/opt/homebrew/include
LIBS = -L/opt/homebrew/lib -ljson-c

# 타겟 실행 파일
SERVER = baseball_server
CLIENT = baseball_client

# 소스 파일
SERVER_SRC = baseball_server.c
CLIENT_SRC = baseball_client.c
PROTOCOL_H = baseball_protocol.h

# 기본 타겟
all: $(SERVER) $(CLIENT)

# 서버 컴파일
$(SERVER): $(SERVER_SRC) $(PROTOCOL_H)
	$(CC) $(CFLAGS) -o $(SERVER) $(SERVER_SRC) $(LIBS)

# 클라이언트 컴파일
$(CLIENT): $(CLIENT_SRC) $(PROTOCOL_H)
	$(CC) $(CFLAGS) -o $(CLIENT) $(CLIENT_SRC) $(LIBS)

# 테스트 실행
test: all
	@echo "=========================================="
	@echo "🎯 숫자 야구 네트워크 게임 테스트 🎯"
	@echo "=========================================="
	@echo "서버 실행: ./$(SERVER) 8080"
	@echo "클라이언트 실행: ./$(CLIENT) 127.0.0.1 8080"
	@echo "=========================================="

# 정리
clean:
	rm -f $(SERVER) $(CLIENT)

# 게임 실행 도우미
run-server:
	./$(SERVER) 8080

run-client:
	./$(CLIENT) 127.0.0.1 8080

# json-c 라이브러리 설치 확인
check-deps:
	@echo "json-c 라이브러리 확인 중..."
	@pkg-config --exists json-c && echo "✅ json-c 설치됨" || echo "❌ json-c 미설치 - 설치 필요: brew install json-c"

.PHONY: all test clean run-server run-client check-deps
