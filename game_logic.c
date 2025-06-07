// game_logic.c
#include "game_logic.h"

// ──────────────────────────────────────────────────────────
// 1) 맵 초기화: Resource 타일(무작위), 나머지 Empty
// ──────────────────────────────────────────────────────────
void init_map(GameState *state) {
    for (int i = 0; i < MAP_HEIGHT; i++) {
        for (int j = 0; j < MAP_WIDTH; j++) {
            // 좌표(i,j)가 맵 범위 내
            // 예시: 확률적으로 10% 정도 Resource 배치
            int r = rand() % 100;
            if (r < 10) {
                state->map[i][j] = TILE_RESOURCE;
            } else {
                state->map[i][j] = TILE_EMPTY;
            }
        }
    }
}

// ──────────────────────────────────────────────────────────
// 2) 게임 상태 초기화
//    - 각 플레이어 상태 초기화 (unit/building count = 0, 자원=0, 기지 미배치),
//    - 맵(16×16) 랜덤 배치
// ──────────────────────────────────────────────────────────
void init_game_state(GameState *state) {
    state->player_num = 0;
    state->last_update = time(NULL);
    state->event_flag = 0;
    init_map(state);
    for (int i = 0; i < MAX_CLIENTS; i++) {
        state->players[i].player_id = i;
        state->players[i].country = 0;
        state->players[i].base_x = -1;
        state->players[i].base_y = -1;
        state->players[i].unit_count = 0;
        state->players[i].building_count = 0;
    }
}

// ──────────────────────────────────────────────────────────
// 3) 플레이어 기지 배치
//    - 맵 범위 내, 빈 타일에만 배치 가능
// ──────────────────────────────────────────────────────────
int place_base(GameState *state, int player_id, int x, int y) {
    if (x < 0 || x >= MAP_WIDTH || y < 0 || y >= MAP_HEIGHT) return -1;
    if (state->map[y][x] != TILE_EMPTY) return -1;
    // 이미 기지를 배치했으면 불가
    if (state->players[player_id].base_x != -1) return -1;

    state->map[y][x] = TILE_BASE;
    state->players[player_id].base_x = x;
    state->players[player_id].base_y = y;
    // 건물 목록에도 추가
    Building b;
    b.owner_id = player_id;
    b.building_id = player_id * 100 + state->players[player_id].building_count;
    b.type = TILE_BASE;
    b.x = x;
    b.y = y;
    b.hp = 100;
    state->players[player_id].buildings[state->players[player_id].building_count++] = b;
    return 0;
}

// ──────────────────────────────────────────────────────────
// 4) 유닛 생산
//    - 자원 소모 예시: Worker=10, Soldier=20, Tank=50, Drone=30
//    - 생산 위치는 항상 기지 주변(예시: base_x±1, base_y±1 중 빈 칸)
// ──────────────────────────────────────────────────────────
int produce_unit(GameState *state, int player_id, UnitType type) {
    PlayerState *ps = &state->players[player_id];
    if (ps->base_x < 0) return -1; // 기지 미배치

    int hp, atk, def;
    switch (type) {
        case UNIT_WORKER: hp = 30; atk = 5; def = 2; break;
        case UNIT_SOLDIER: hp = 50; atk = 10; def = 5; break;
        case UNIT_TANK: hp = 100; atk = 20; def = 15; break;
        case UNIT_DRONE: hp = 20; atk = 15; def = 1; break;
        default: return -1;
    }
    // 자원 체크/차감 제거

    // 생산 위치 탐색 (base 주변 8칸 중 빈곳)
    int bx = ps->base_x, by = ps->base_y;
    int found_x = -1, found_y = -1;
    for (int dy = -1; dy <= 1; dy++) {
        for (int dx = -1; dx <= 1; dx++) {
            int nx = bx + dx, ny = by + dy;
            if (nx < 0 || nx >= MAP_WIDTH || ny < 0 || ny >= MAP_HEIGHT) continue;
            if (state->map[ny][nx] == TILE_EMPTY) {
                found_x = nx;
                found_y = ny;
                goto PLACE_UNIT;
            }
        }
    }
PLACE_UNIT:
    if (found_x == -1) return -1; // 주위에 빈칸 없음

    Unit u;
    u.owner_id = player_id;
    u.unit_id = player_id * 100 + ps->unit_count;
    u.type = type;
    u.x = found_x; u.y = found_y;
    u.hp = hp;
    u.attack = atk;
    u.defense = def;
    u.moving = 0;
    ps->units[ps->unit_count++] = u;
    return 0;
}

// ──────────────────────────────────────────────────────────
// 5) 유닛 이동
//    - 맵 범위 내, 연속적인 1칸 이동만 허용(예시)
// ──────────────────────────────────────────────────────────
int move_unit(GameState *state, int player_id, int unit_id, int new_x, int new_y) {
    if (new_x < 0 || new_x >= MAP_WIDTH || new_y < 0 || new_y >= MAP_HEIGHT) return -1;
    // 해당 유닛 찾기
    PlayerState *ps = &state->players[player_id];
    for (int i = 0; i < ps->unit_count; i++) {
        if (ps->units[i].unit_id == unit_id) {
            int ox = ps->units[i].x;
            int oy = ps->units[i].y;
            // 인접하다면(맨해튼 거리 1)
            if (abs(new_x - ox) + abs(new_y - oy) == 1) {
                // 이동 가능 조건 예시: 목적지가 Base/Resource/Tower가 아니어야 함
                if (state->map[new_y][new_x] == TILE_EMPTY) {
                    ps->units[i].x = new_x;
                    ps->units[i].y = new_y;
                    ps->units[i].moving = 1;
                    return 0;
                }
            }
        }
    }
    return -1;
}

// ──────────────────────────────────────────────────────────
// 6) 유닛 공격
//    - 간단: 공격하는 유닛과 피격 유닛이 ‘인접’해 있어야 함
//    - 공격력–방어력 계산하여 HP 차감
// ──────────────────────────────────────────────────────────
int attack_unit(GameState *state, int player_id, int attacker_id, int target_id) {
    Unit *att = NULL, *tgt = NULL;
    // attacker 찾기
    for (int i = 0; i < state->players[player_id].unit_count; i++) {
        if (state->players[player_id].units[i].unit_id == attacker_id) {
            att = &state->players[player_id].units[i];
            break;
        }
    }
    if (!att) return -1;
    // target 찾기 (모든 플레이어 순회 중)
    for (int pid = 0; pid < MAX_CLIENTS; pid++) {
        for (int i = 0; i < state->players[pid].unit_count; i++) {
            if (state->players[pid].units[i].unit_id == target_id) {
                tgt = &state->players[pid].units[i];
                break;
            }
        }
        if (tgt) break;
    }
    if (!tgt) return -1;
    // 인접 거리 체크 (맨해튼 거리 <= 1)
    if (abs(att->x - tgt->x) + abs(att->y - tgt->y) != 1) return -1;
    // 대미지 계산
    int damage = att->attack - tgt->defense;
    if (damage < 1) damage = 1;
    tgt->hp -= damage;
    // 유닛 파괴 시, 리스트에서 제거 (간단하게 뒤로 채워 넣기)
    if (tgt->hp <= 0) {
        int owner = tgt->owner_id;
        PlayerState *tps = &state->players[owner];
        int idx = -1;
        for (int i = 0; i < tps->unit_count; i++) {
            if (tps->units[i].unit_id == target_id) {
                idx = i;
                break;
            }
        }
        if (idx >= 0) {
            // 마지막 유닛을 덮어쓰기
            tps->units[idx] = tps->units[tps->unit_count - 1];
            tps->unit_count--;
        }
    }
    return 0;
}

// ──────────────────────────────────────────────────────────
// 7) 기술 연구(업그레이드)
//    - 단순: tech_level++ 및 비용 차감 (예시: 매 레벨마다 50 자원)
// ──────────────────────────────────────────────────────────

// ──────────────────────────────────────────────────────────
// 8) 이벤트 발생 (10초마다 무작위로 지진 또는 정전 발생 예시)
// ──────────────────────────────────────────────────────────
void trigger_event(GameState *state) {
    long now = current_millis();
    // 마지막 업데이트 후 10초(=10,000ms) 지났으면 한 번
    if (now - (state->last_update * 1000) >= 10000) {
        int r = rand() % 100;
        if (r < 20) {
            state->event_flag = 1; // 지진
        } else if (r < 40) {
            state->event_flag = 2; // 정전
        } else {
            state->event_flag = 0; // 이벤트 없음
        }
        state->last_update = now / 1000;
    }
}

// ──────────────────────────────────────────────────────────
// 9) 전체 게임 상태를 주기적으로 업데이트하는 함수
//    - 자원 획득 (예: 매 업데이트마다 5 자원씩 추가)
//    - 이벤트 발생 여부 체크
//    - 그 외 동기화 로직 필요 시 추가
// ──────────────────────────────────────────────────────────
void update_game(GameState *state) {
    // 자원 추가 제거
    // for (int i = 0; i < MAX_CLIENTS; i++) {
    //     if (state->players[i].base_x != -1) {
    //         state->players[i].resources += 5;
    //     }
    // }
    // 이벤트 발생 처리
    trigger_event(state);

    // 게임 오버 체크: 2명 이상 기지 살아있을 때만 게임 오버 체크
    int alive_count = 0;
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (state->players[i].base_x != -1 && !is_base_destroyed(state, i)) {
            alive_count++;
        }
    }
    // 게임 오버는 2명 이상 기지 배치 후 한 명만 남았을 때만 발생
    static int ever_had_two_bases = 0;
    if (alive_count >= 2) {
        ever_had_two_bases = 1;
        if (state->event_flag == 99) state->event_flag = 0;
    } else if (alive_count == 1 && ever_had_two_bases) {
        state->event_flag = 99;
        state->last_update = time(NULL);
    } else {
        state->event_flag = 0;
    }
}

// ──────────────────────────────────────────────────────────
// 10) GameState를 JSON으로 직렬화
//     - 플레이어별 유닛, 건물, 자원, 위치 정보를 JSON 배열로 생성
//     - 맵 정보는 2차원 배열로 직렬화 (숫자값)
// ──────────────────────────────────────────────────────────
struct json_object *game_state_to_json(const GameState *state) {
    struct json_object *jstate = json_object_new_object();
    json_object_object_add(jstate, "player_num", json_object_new_int(state->player_num));
    json_object_object_add(jstate, "event_flag", json_object_new_int(state->event_flag));

    struct json_object *jplayers = json_object_new_array();
    for (int i = 0; i < MAX_CLIENTS; i++) {
        const PlayerState *ps = &state->players[i];
        if (ps->base_x == -1) continue;
        struct json_object *jpl = json_object_new_object();
        json_object_object_add(jpl, "player_id", json_object_new_int(ps->player_id));
        json_object_object_add(jpl, "country", json_object_new_int(ps->country));
        // json_object_object_add(jpl, "tech_level", json_object_new_int(ps->tech_level)); // 연구 제거

        // 10-2-1) 기지 위치 추가
        struct json_object *jbase = json_object_new_object();
        json_object_object_add(jbase, "x", json_object_new_int(ps->base_x));
        json_object_object_add(jbase, "y", json_object_new_int(ps->base_y));
        json_object_object_add(jpl, "base", jbase);

        // 10-2-2) 유닛 배열
        struct json_object *junits = json_object_new_array();
        for (int u = 0; u < ps->unit_count; u++) {
            const Unit *unit = &ps->units[u];
            struct json_object *ju = json_object_new_object();
            json_object_object_add(ju, "unit_id", json_object_new_int(unit->unit_id));
            json_object_object_add(ju, "owner_id", json_object_new_int(unit->owner_id));
            json_object_object_add(ju, "type", json_object_new_int(unit->type));
            json_object_object_add(ju, "x", json_object_new_int(unit->x));
            json_object_object_add(ju, "y", json_object_new_int(unit->y));
            json_object_object_add(ju, "hp", json_object_new_int(unit->hp));
            json_object_object_add(ju, "moving", json_object_new_int(unit->moving));
            json_object_array_add(junits, ju);
        }
        json_object_object_add(jpl, "units", junits);

        // 10-2-3) 건물 배열
        struct json_object *jbuilds = json_object_new_array();
        for (int b = 0; b < ps->building_count; b++) {
            const Building *bd = &ps->buildings[b];
            struct json_object *jb = json_object_new_object();
            json_object_object_add(jb, "building_id", json_object_new_int(bd->building_id));
            json_object_object_add(jb, "owner_id", json_object_new_int(bd->owner_id));
            json_object_object_add(jb, "type", json_object_new_int(bd->type));
            json_object_object_add(jb, "x", json_object_new_int(bd->x));
            json_object_object_add(jb, "y", json_object_new_int(bd->y));
            json_object_object_add(jb, "hp", json_object_new_int(bd->hp));
            json_object_array_add(jbuilds, jb);
        }
        json_object_object_add(jpl, "buildings", jbuilds);

        json_object_array_add(jplayers, jpl);
    }
    json_object_object_add(jstate, "players", jplayers);

    // 10-3) 맵 타일 정보 2차원 배열
    struct json_object *jmap = json_object_new_array();
    for (int i = 0; i < MAP_HEIGHT; i++) {
        struct json_object *jrow = json_object_new_array();
        for (int j = 0; j < MAP_WIDTH; j++) {
            json_object_array_add(jrow, json_object_new_int(state->map[i][j]));
        }
        json_object_array_add(jmap, jrow);
    }
    json_object_object_add(jstate, "map", jmap);

    return jstate;
}

// ──────────────────────────────────────────────────────────
// 11) 이벤트를 JSON으로 변환
// ──────────────────────────────────────────────────────────
struct json_object *event_to_json(int event_flag) {
    struct json_object *jev = json_object_new_object();
    if (event_flag == 1) {
        json_object_object_add(jev, "event", json_object_new_string("earthquake"));
    } else if (event_flag == 2) {
        json_object_object_add(jev, "event", json_object_new_string("blackout"));
    } else {
        json_object_object_add(jev, "event", json_object_new_string("none"));
    }
    return jev;
}

// 기지 파괴 여부 체크: 하나라도 살아있으면 0, 아니면 1
int is_base_destroyed(const GameState *state, int player_id) {
    const PlayerState *ps = &state->players[player_id];
    for (int i = 0; i < ps->building_count; i++) {
        if (ps->buildings[i].type == TILE_BASE && ps->buildings[i].hp > 0)
            return 0; // 아직 기지 살아있음
    }
    return 1; // 기지 모두 파괴됨
}
