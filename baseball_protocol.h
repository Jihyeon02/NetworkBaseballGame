// baseball_protocol.h - 숫자 야구 네트워크 게임 프로토콜 정의
// 네트워크 프로그래밍 과제용 TCP/JSON 기반 통신 프로토콜
#ifndef BASEBALL_PROTOCOL_H
#define BASEBALL_PROTOCOL_H

#include <json-c/json.h>
#include <sys/time.h>    // 타임아웃 처리용
#include <sys/socket.h>  // 소켓 옵션 상수용
#include <errno.h>       // 에러 코드 처리용
#include <time.h>        // time() 함수용
#include <string.h>      // strlen() 함수용

// ──────────────────────────────────────────────────────────
// 1) 네트워크 설정 및 타임아웃 상수
// ──────────────────────────────────────────────────────────
#define NETWORK_TIMEOUT_SEC     30      // 30초 네트워크 타임아웃
#define HEARTBEAT_INTERVAL_SEC  10      // 10초마다 연결 상태 확인
#define MAX_RETRY_COUNT         3       // 최대 재시도 횟수
#define RECV_TIMEOUT_SEC        5       // recv() 타임아웃 (초)
#define SEND_TIMEOUT_SEC        5       // send() 타임아웃 (초)

// ──────────────────────────────────────────────────────────
// 2) 서버⇄클라이언트 간 메시지 Action 문자열 정의
// ──────────────────────────────────────────────────────────
#define ACTION_JOIN           "join"           // 플레이어가 서버에 접속
#define ACTION_ASSIGN_ID      "assign_id"      // 서버가 플레이어에게 ID 할당
#define ACTION_WAIT_PLAYER    "wait_player"    // 상대방 대기 중
#define ACTION_GAME_START     "game_start"     // 게임 시작 (2명 모두 접속)
#define ACTION_SET_NUMBER     "set_number"     // 플레이어가 3자리 숫자 설정
#define ACTION_NUMBER_SET     "number_set"     // 숫자 설정 완료 응답
#define ACTION_YOUR_TURN      "your_turn"      // 당신의 턴
#define ACTION_WAIT_TURN      "wait_turn"      // 상대방 턴 대기
#define ACTION_GUESS          "guess"          // 숫자 추측
#define ACTION_GUESS_RESULT   "guess_result"   // 추측 결과 (스트라이크/볼)
#define ACTION_GAME_OVER      "game_over"      // 게임 종료 (승리/패배)
#define ACTION_ERROR          "error"          // 오류 메시지
#define ACTION_HEARTBEAT      "heartbeat"      // 연결 상태 확인 (새로 추가)
#define ACTION_TIMEOUT        "timeout"        // 타임아웃 발생 (새로 추가)

// ──────────────────────────────────────────────────────────
// 3) 게임 설정 상수
// ──────────────────────────────────────────────────────────
#define MAX_CLIENTS     2       // 1:1 대전 게임
#define NUMBER_LENGTH   3       // 3자리 숫자 사용
#define MAX_ATTEMPTS   10       // 최대 10번 추측 허용
#define BUF_SIZE     4096       // 네트워크 버퍼 크기

// ──────────────────────────────────────────────────────────
// 4) 게임 상태 열거형 (게임 전체 진행 상태)
// ──────────────────────────────────────────────────────────
typedef enum {
    GAME_WAITING = 0,    // 플레이어 연결 대기 중
    GAME_SETTING,        // 플레이어들이 비밀 숫자 설정 중
    GAME_PLAYING,        // 실제 게임 진행 중 (턴제)
    GAME_FINISHED        // 게임 종료 (승부 결정)
} GameState;

// ──────────────────────────────────────────────────────────
// 5) 플레이어 상태 열거형 (개별 플레이어 상태)
// ──────────────────────────────────────────────────────────
typedef enum {
    PLAYER_WAITING = 0,  // 서버 연결 후 상대방 대기 중
    PLAYER_SETTING,      // 비밀 숫자 설정 중
    PLAYER_READY,        // 게임 준비 완료 상태
    PLAYER_TURN,         // 현재 내 턴 (추측 가능)
    PLAYER_WAITING_TURN  // 상대방 턴 대기 중
} PlayerState;

// ──────────────────────────────────────────────────────────
// 6) 플레이어 정보 구조체 (각 플레이어의 상태와 데이터 관리)
// ──────────────────────────────────────────────────────────
typedef struct {
    int sockfd;                     // 소켓 파일 디스크립터
    int player_id;                  // 플레이어 ID (0 또는 1)
    int connected;                  // 연결 상태 (1: 연결됨, 0: 연결 해제)
    PlayerState state;              // 현재 플레이어 상태
    char secret_number[4];          // 비밀 숫자 (3자리 + null terminator)
    int attempts;                   // 현재까지 추측 시도 횟수
    int is_winner;                  // 승리 여부 (1: 승리, 0: 미승리)
    time_t last_activity;           // 마지막 활동 시간 (타임아웃 체크용)
    int retry_count;                // 네트워크 재시도 횟수
} PlayerInfo;

// ──────────────────────────────────────────────────────────
// 7) 게임 매니저 구조체 (전체 게임 상태 관리)
// ──────────────────────────────────────────────────────────
typedef struct {
    GameState state;                // 현재 게임 상태
    PlayerInfo players[MAX_CLIENTS]; // 모든 플레이어 정보 배열
    int current_turn;               // 현재 턴인 플레이어 ID (0 또는 1)
    int players_ready;              // 게임 준비 완료된 플레이어 수
    time_t game_start_time;         // 게임 시작 시간
    time_t last_heartbeat;          // 마지막 연결 상태 확인 시간
} GameManager;

// ──────────────────────────────────────────────────────────
// 8) 추측 결과 구조체 (스트라이크/볼 계산 결과)
// ──────────────────────────────────────────────────────────
typedef struct {
    int strikes;        // 스트라이크 수 (숫자와 위치 모두 정확)
    int balls;          // 볼 수 (숫자는 맞지만 위치 틀림)
    int is_correct;     // 정답 여부 (3스트라이크 = 정답)
} GuessResult;

// ──────────────────────────────────────────────────────────
// 9) 네트워크 지연 처리 함수들
// ──────────────────────────────────────────────────────────

/**
 * 소켓에 타임아웃 설정
 * @param sockfd: 소켓 파일 디스크립터
 * @param timeout_sec: 타임아웃 시간 (초)
 * @return: 성공 시 0, 실패 시 -1
 */
static inline int set_socket_timeout(int sockfd, int timeout_sec) {
    struct timeval timeout;
    timeout.tv_sec = timeout_sec;
    timeout.tv_usec = 0;
    
    // 수신 타임아웃 설정
    if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0) {
        return -1;
    }
    
    // 송신 타임아웃 설정
    if (setsockopt(sockfd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)) < 0) {
        return -1;
    }
    
    return 0;
}

/**
 * 플레이어 활동 시간 업데이트 (타임아웃 방지용)
 * @param player: 업데이트할 플레이어 정보
 */
static inline void update_player_activity(PlayerInfo *player) {
    player->last_activity = time(NULL);
}

/**
 * 플레이어 타임아웃 체크
 * @param player: 체크할 플레이어 정보
 * @return: 타임아웃 시 1, 정상 시 0
 */
static inline int is_player_timeout(PlayerInfo *player) {
    time_t current_time = time(NULL);
    return (current_time - player->last_activity) > NETWORK_TIMEOUT_SEC;
}

// ──────────────────────────────────────────────────────────
// 10) JSON 메시지 생성 헬퍼 함수들
// ──────────────────────────────────────────────────────────

/**
 * 기본 JSON 메시지 객체 생성
 * @param action: 메시지 액션 타입
 * @return: JSON 객체 포인터
 */
static inline struct json_object *create_message(const char *action) {
    struct json_object *jmsg = json_object_new_object();
    json_object_object_add(jmsg, "action", json_object_new_string(action));
    return jmsg;
}

/**
 * 에러 메시지 JSON 객체 생성
 * @param message: 에러 메시지 내용
 * @return: 에러 JSON 객체 포인터
 */
static inline struct json_object *create_error(const char *message) {
    struct json_object *jmsg = create_message(ACTION_ERROR);
    json_object_object_add(jmsg, "message", json_object_new_string(message));
    return jmsg;
}

/**
 * 타임아웃 메시지 JSON 객체 생성
 * @param reason: 타임아웃 발생 이유
 * @return: 타임아웃 JSON 객체 포인터
 */
static inline struct json_object *create_timeout_message(const char *reason) {
    struct json_object *jmsg = create_message(ACTION_TIMEOUT);
    json_object_object_add(jmsg, "reason", json_object_new_string(reason));
    return jmsg;
}

/**
 * 하트비트(연결 확인) 메시지 JSON 객체 생성
 * @return: 하트비트 JSON 객체 포인터
 */
static inline struct json_object *create_heartbeat_message(void) {
    struct json_object *jmsg = create_message(ACTION_HEARTBEAT);
    json_object_object_add(jmsg, "timestamp", json_object_new_string("heartbeat"));
    return jmsg;
}

// ──────────────────────────────────────────────────────────
// 11) 게임 로직 검증 함수들
// ──────────────────────────────────────────────────────────

/**
 * 3자리 숫자 유효성 검사
 * - 정확히 3자리인지 확인
 * - 모든 문자가 숫자인지 확인  
 * - 중복 숫자가 없는지 확인
 * @param number: 검사할 숫자 문자열
 * @return: 유효하면 1, 무효하면 0
 */
static inline int is_valid_number(const char *number) {
    if (strlen(number) != NUMBER_LENGTH) return 0;
    
    // 각 자리수가 숫자인지 확인
    for (int i = 0; i < NUMBER_LENGTH; i++) {
        if (number[i] < '0' || number[i] > '9') return 0;
    }
    
    // 중복 숫자 확인 (예: 112, 233 등은 무효)
    for (int i = 0; i < NUMBER_LENGTH; i++) {
        for (int j = i + 1; j < NUMBER_LENGTH; j++) {
            if (number[i] == number[j]) return 0;
        }
    }
    
    return 1;
}

/**
 * 스트라이크와 볼 계산 함수
 * - 스트라이크: 숫자와 위치가 모두 정확한 경우
 * - 볼: 숫자는 포함되어 있지만 위치가 틀린 경우
 * @param secret: 정답 숫자 (3자리)
 * @param guess: 추측 숫자 (3자리)
 * @return: GuessResult 구조체 (스트라이크, 볼, 정답여부)
 */
static inline GuessResult calculate_result(const char *secret, const char *guess) {
    GuessResult result = {0, 0, 0};
    
    // 1단계: 스트라이크 계산 (같은 위치의 같은 숫자)
    for (int i = 0; i < NUMBER_LENGTH; i++) {
        if (secret[i] == guess[i]) {
            result.strikes++;
        }
    }
    
    // 2단계: 볼 계산 (다른 위치의 같은 숫자, 스트라이크 제외)
    for (int i = 0; i < NUMBER_LENGTH; i++) {
        if (secret[i] != guess[i]) {  // 스트라이크가 아닌 경우만
            for (int j = 0; j < NUMBER_LENGTH; j++) {
                if (i != j && secret[i] == guess[j] && secret[j] != guess[j]) {
                    result.balls++;
                    break;  // 같은 숫자를 중복 카운팅하지 않음
                }
            }
        }
    }
    
    // 3단계: 정답 확인 (3스트라이크 = 정답)
    result.is_correct = (result.strikes == NUMBER_LENGTH);
    
    return result;
}

#endif // BASEBALL_PROTOCOL_H 