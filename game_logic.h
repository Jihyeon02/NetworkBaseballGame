// game_logic.h
#ifndef GAME_LOGIC_H
#define GAME_LOGIC_H

#include <json-c/json.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>
#include "protocol.h"

// ──────────────────────────────────────────────────────────
// 1) 맵 타일 종류 정의
// ──────────────────────────────────────────────────────────
typedef enum {
    TILE_EMPTY = 0,
    TILE_RESOURCE,
    TILE_BASE,
    TILE_TOWER
} TileType;

// ──────────────────────────────────────────────────────────
// 2) 유닛 종류 정의
// ──────────────────────────────────────────────────────────
typedef enum {
    UNIT_WORKER = 0,
    UNIT_SOLDIER,
    UNIT_TANK,
    UNIT_DRONE
} UnitType;

// ──────────────────────────────────────────────────────────
// 3) 유닛 상태 구조체
// ──────────────────────────────────────────────────────────
typedef struct {
    int unit_id;            // 유닛 고유 번호 (player_id * 100 + 순번)
    int owner_id;           // 이 유닛을 소유한 플레이어의 ID
    UnitType type;          // 유닛 종류
    int x, y;               // 현재 위치 (맵 좌표)
    int hp;                 // 체력
    int attack;             // 공격력
    int defense;            // 방어력
    int moving;             // 1: 이동 중, 0: 정지
} Unit;

// ──────────────────────────────────────────────────────────
// 4) 건물 상태 구조체
// ──────────────────────────────────────────────────────────
typedef struct {
    int building_id;       // 건물 고유 번호 (player_id * 100 + 순번)
    int owner_id;          // 소유한 플레이어 ID
    TileType type;         // 건물 종류(TILE_BASE 또는 TILE_TOWER)
    int x, y;              // 건물 위치
    int hp;                // 체력
} Building;

// ──────────────────────────────────────────────────────────
// 5) 플레이어별 상태(기지 위치, 자원량, 유닛/건물 리스트 등)
// ──────────────────────────────────────────────────────────
typedef struct {
    int player_id;
    int country;
    int base_x, base_y;
    int unit_count;
    Unit units[100];
    int building_count;
    Building buildings[50];
    // int tech_level; // 연구 제거
} PlayerState;

// ──────────────────────────────────────────────────────────
// 6) 전체 게임 상태 구조체
// ──────────────────────────────────────────────────────────
typedef struct {
    time_t last_update;               // 마지막 상태 갱신 시각
    int player_num;                   // 현재 접속된 플레이어 수
    PlayerState players[MAX_CLIENTS];// 플레이어별 상태
    TileType map[MAP_HEIGHT][MAP_WIDTH]; // 맵 타일 정보
    int event_flag;                   // 0: 없음, 1: 지진, 2: 정전 등
} GameState;

// ──────────────────────────────────────────────────────────
// 7) 밀리초 단위로 현재 시각 얻기 (실시간 업데이트 시 사용)
// ──────────────────────────────────────────────────────────
static inline long current_millis() {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

// ──────────────────────────────────────────────────────────
// 8) 함수 선언부
//    - 초기화, 유닛/건물 추가, 이동, 공격, 이벤트 처리, JSON 변환 등
// ──────────────────────────────────────────────────────────

// 8-1) 게임 상태 초기화 (맵, 플레이어)
void init_game_state(GameState *state);

// 8-2) 플레이어가 기지를 배치했을 때 호출
//      (base_x, base_y가 유효 범위 내에 있는지 확인 후 설정)
int place_base(GameState *state, int player_id, int x, int y);

// 8-3) 유닛 생산 요청 처리 (Worker, Soldier, Tank, Drone 중 하나)
//      생산 성공 시 0, 실패 시 -1 반환
int produce_unit(GameState *state, int player_id, UnitType type);

// 8-4) 유닛 이동 요청 처리
//      (해당 유닛 있는지, 목적지 범위, 충돌 체크 등 간단 처리)
//      성공 시 0, 실패 시 -1
int move_unit(GameState *state, int player_id, int unit_id, int new_x, int new_y);

// 8-5) 유닛 공격 요청 처리
int attack_unit(GameState *state, int player_id, int attacker_id, int target_id);

// 8-6) 기술 연구(업그레이드) 요청 처리
// int research_tech(GameState *state, int player_id); // 삭제
// 8-7) 서버가 주기적으로 호출하여 전체 상태 업데이트
void update_game(GameState *state);

// 8-8) 발생한 게임 상태를 JSON으로 직렬화
struct json_object *game_state_to_json(const GameState *state);

// 8-9) 발생한 이벤트를 JSON으로 직렬화
struct json_object *event_to_json(int event_flag);

// 8-10) 플레이어의 기지가 파괴되었는지 확인
int is_base_destroyed(const GameState *state, int player_id);

#endif // GAME_LOGIC_H
