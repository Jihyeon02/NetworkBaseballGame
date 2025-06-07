// protocol.h
#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <json-c/json.h>

// ──────────────────────────────────────────────────────────
// 1) 서버⇄클라이언트 간 메시지 Action 문자열 정의
// ──────────────────────────────────────────────────────────
#define ACTION_JOIN          "join"          // 플레이어가 서버에 접속했음을 알림
#define ACTION_ASSIGN_ID     "assign_id"     // 서버가 플레이어에게 ID를 할당함
#define ACTION_COUNTRY_CHOOSE "country"      // 플레이어가 국가(진영) 선택
#define ACTION_UPDATE_STATE  "update_state"  // 서버가 현재 전체 게임 상태를 전송
#define ACTION_COMMAND       "command"       // 클라이언트가 특정 액션(유닛 이동, 생산 등)을 요청
#define ACTION_EVENT         "event"         // 서버가 발생한 이벤트(예: 지진, 정전 등) 통보
#define ACTION_GAME_OVER     "game_over"     // 서버가 승리/패배 결과 전송
#define ACTION_ERROR          "error"

// ──────────────────────────────────────────────────────────
// 2) 최대 연결 가능한 클라이언트 수 / 맵 크기 / ID 상수
// ──────────────────────────────────────────────────────────
#define MAX_CLIENTS    8      // 최대 플레이어(국가) 수. 예시로 8개 국가 허용
#define MAP_WIDTH     16
#define MAP_HEIGHT    16
#define MAP_SIZE 16    // 맵 크기 (0 ~ 15)
#define MAX_UNIT_TYPE 3  // 예시: Worker(0), Soldier(1), Tank(2) 등
#define BUF_SIZE    4096

// ──────────────────────────────────────────────────────────
// 3) 클라이언트→서버로 전송하는 커맨드 종류
//    (value 필드 안에 JSON 객체 형태로 인자 전달)
// ──────────────────────────────────────────────────────────
typedef enum {
    CMD_NONE = 0,
    CMD_PLACE_BASE,     // 기지 배치 (필수) 
    CMD_PRODUCE_UNIT,   // 유닛 생산
    CMD_MOVE_UNIT,      // 유닛 이동
    CMD_ATTACK_UNIT,    // 유닛 공격
    CMD_REQUEST_STATE   // 현재 게임 상태 재요청
    
} CommandType;

// ──────────────────────────────────────────────────────────
// 4) 클라이언트가 서버로 보낼 커맨드 메시지 생성 헬퍼
//    (JSON 객체에 {"action":"command", "type":<int>, "payload":{...}} 구조를 생성)
// ──────────────────────────────────────────────────────────
static inline struct json_object *make_command_msg(CommandType type, struct json_object *payload) {
    struct json_object *job = json_object_new_object();
    json_object_object_add(job, "action", json_object_new_string(ACTION_COMMAND));
    json_object_object_add(job, "type", json_object_new_int(type));
    if (payload != NULL) {
        json_object_object_add(job, "payload", payload);
    }
    return job;
}

// ──────────────────────────────────────────────────────────
// 5) 플레이어 정보 구조체 선언
// ──────────────────────────────────────────────────────────
typedef struct {
    int sockfd;              // 클라이언트 소켓 fd
    int player_id;           // 서버가 할당하는 고유 ID (0 ~ MAX_CLIENTS-1)
    int country;             // 선택한 국가 번호 (1 ~ MAX_CLIENTS)
    int connected;           // 1이면 접속 상태, 0이면 미접속
} PlayerInfo;

// ──────────────────────────────────────────────────────────
// 6) 패킷 송수신 버퍼 크기 정의
// ──────────────────────────────────────────────────────────
enum { 
    HEADER_LEN = 2         // 메시지 길이(2바이트) + JSON Payload
};

// ──────────────────────────────────────────────────────────
// 7) 메시지 읽기/쓰기 헬퍼 함수들 (구현은 server.c / client.c 내부에)
//    - send_json(int fd, struct json_object *jobj)
//    - recv_json(int fd)
// ──────────────────────────────────────────────────────────
// ※ 주의: send_json은 먼저 길이 필드(2바이트, 네트워크 바이트 순서)를 보내고,
//    그 뒤에 JSON 문자열(body)을 전송. recv_json은 먼저 길이 필드를 읽고,
//    해당 길이만큼 바이트를 읽어 json_parse함.

#endif // PROTOCOL_H
