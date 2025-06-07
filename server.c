// server.c
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <arpa/inet.h>
#include <time.h>

#include "protocol.h"
#include "game_logic.h"

// ──────────────────────────────────────────────────────────
// 전역 변수: 모든 플레이어 정보 및 게임 상태
// ──────────────────────────────────────────────────────────
PlayerInfo players[MAX_CLIENTS];
GameState gstate;

// ──────────────────────────────────────────────────────────
// 헬퍼: 소켓에 JSON 메시지를 보내기 (2바이트 길이 + JSON 문자열)
// ──────────────────────────────────────────────────────────
int send_json(int fd, struct json_object *jobj) {
    const char *s = json_object_to_json_string(jobj);
    int len = strlen(s);
    uint16_t netlen = htons(len);
    if (send(fd, &netlen, sizeof(netlen), 0) != sizeof(netlen)) {
        return -1;
    }
    if (send(fd, s, len, 0) != len) {
        return -1;
    }
    return 0;
}

// ──────────────────────────────────────────────────────────
// 헬퍼: 소켓으로부터 JSON 메시지 받기 (2바이트 길이 + JSON 파싱)
// ──────────────────────────────────────────────────────────
struct json_object *recv_json(int fd) {
    uint16_t netlen;
    ssize_t n = recv(fd, &netlen, sizeof(netlen), MSG_WAITALL);
    if (n <= 0) return NULL;
    int len = ntohs(netlen);
    if (len <= 0 || len > BUF_SIZE) return NULL;
    char buf[BUF_SIZE + 1];
    n = recv(fd, buf, len, MSG_WAITALL);
    if (n <= 0) return NULL;
    buf[len] = '\0';
    return json_tokener_parse(buf);
}

// ──────────────────────────────────────────────────────────
// 새로운 플레이어가 접속했을 때 호출
//  - 소켓 accept → players 배열에 등록 → 플레이어 ID 할당 → JSON으로 응답
// ──────────────────────────────────────────────────────────
void handle_new_connection(int listen_fd) {
    struct sockaddr_in cli_addr;
    socklen_t cli_len = sizeof(cli_addr);
    int conn_fd = accept(listen_fd, (struct sockaddr *)&cli_addr, &cli_len);
    if (conn_fd < 0) {
        perror("accept");
        return;
    }
    // 빈 슬롯 찾기
    int pid = -1;
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (!players[i].connected) {
            pid = i;
            break;
        }
    }
    if (pid < 0) {
        // 이미 최대 플레이어 수 도달
        close(conn_fd);
        return;
    }
    // 플레이어 정보 초기화
    players[pid].sockfd     = conn_fd;
    players[pid].player_id  = pid;
    players[pid].country    = 0; // 아직 선택 안됨
    players[pid].connected  = 1;
    gstate.player_num++;

    // 클라이언트에게 할당된 player_id 전송
    struct json_object *jmsg = json_object_new_object();
    json_object_object_add(jmsg, "action", json_object_new_string(ACTION_ASSIGN_ID));
    json_object_object_add(jmsg, "player_id", json_object_new_int(pid));
    send_json(conn_fd, jmsg);
    json_object_put(jmsg);

    printf("[Server] New player connected: ID=%d, IP=%s\n",
           pid, inet_ntoa(cli_addr.sin_addr));
}

// ──────────────────────────────────────────────────────────
// 클라이언트로부터 받은 JSON 메시지 처리
// ──────────────────────────────────────────────────────────
void handle_client_msg(int pid, fd_set *master_set) {
    int fd = players[pid].sockfd;
    struct json_object *jmsg = recv_json(fd);
    if (!jmsg) {
        // 클라이언트 연결 종료
        printf("[Server] Player %d disconnected\n", pid);
        close(fd);
        players[pid].connected = 0;
        gstate.player_num--;
        if (master_set) FD_CLR(fd, master_set); // FD_CLR 추가
        if (jmsg) json_object_put(jmsg);
        return;
    }

    // action 필드 확인
    struct json_object *jact = NULL;
    if (!json_object_object_get_ex(jmsg, "action", &jact)) {
        json_object_put(jmsg);
        return;
    }
    const char *action = json_object_get_string(jact);

    // ── 국가 선택 처리────────────────────────────────────
    if (strcmp(action, ACTION_COUNTRY_CHOOSE) == 0) {
        struct json_object *jct = NULL;
        if (json_object_object_get_ex(jmsg, "country", &jct)) {
            int country = json_object_get_int(jct);
            // 중복 체크: 이미 선택된 국가 금지
            int taken = 0;
            if (country < 1 || country > MAX_CLIENTS) {
                taken = 1;
            } else {
                for (int i = 0; i < MAX_CLIENTS; i++) {
                    if (i != pid && players[i].connected && players[i].country == country) {
                        taken = 1;
                        break;
                    }
                }
            }
            if (taken) {
                struct json_object *jerr = json_object_new_object();
                json_object_object_add(jerr, "action", json_object_new_string(ACTION_ERROR));
                json_object_object_add(jerr, "message", json_object_new_string("Country already taken or invalid. Choose another."));
                send_json(players[pid].sockfd, jerr);
                json_object_put(jerr);
            } else {
                players[pid].country = country;
                gstate.players[pid].country = country;
                // 성공 응답
                struct json_object *jok = json_object_new_object();
                json_object_object_add(jok, "action", json_object_new_string("country_ok"));
                send_json(players[pid].sockfd, jok);
                json_object_put(jok);
                printf("[Server] Player %d chose country %d\n", pid, country);
            }
        }
    }
    // ── 클라이언트 커맨드 처리────────────────────────────
    else if (strcmp(action, ACTION_COMMAND) == 0) {
        struct json_object *jtype = NULL;
        if (!json_object_object_get_ex(jmsg, "type", &jtype)) {
            json_object_put(jmsg);
            return;
        }
        int cmd = json_object_get_int(jtype);

        struct json_object *jpl = NULL;
        // REQUEST_STATE만 payload가 없으므로 예외
        if (cmd != CMD_REQUEST_STATE) {
            if (!json_object_object_get_ex(jmsg, "payload", &jpl)) {
                json_object_put(jmsg);
                return;
            }
        }

        int result = -1;
        switch (cmd) {
            case CMD_PLACE_BASE: {
                struct json_object *jx = NULL, *jy = NULL;
                json_object_object_get_ex(jpl, "x", &jx);
                json_object_object_get_ex(jpl, "y", &jy);
                int x = json_object_get_int(jx);
                int y = json_object_get_int(jy);
                result = place_base(&gstate, pid, x, y);
                if (result < 0) {
                    struct json_object *jerr = json_object_new_object();
                    json_object_object_add(jerr, "action",
                                           json_object_new_string(ACTION_ERROR));
                    json_object_object_add(
                        jerr, "message",
                        json_object_new_string(
                            "Place base failed (invalid position or occupied)"));
                    send_json(players[pid].sockfd, jerr);
                    json_object_put(jerr);
                }
                break;
            }
            case CMD_PRODUCE_UNIT: {
                struct json_object *jtype_u = NULL;
                json_object_object_get_ex(jpl, "unit_type", &jtype_u);
                int ut = json_object_get_int(jtype_u);
                result = produce_unit(&gstate, pid, (UnitType)ut);
                if (result < 0) {
                    struct json_object *jerr = json_object_new_object();
                    json_object_object_add(jerr, "action",
                                           json_object_new_string(ACTION_ERROR));
                    json_object_object_add(
                        jerr, "message",
                        json_object_new_string(
                            "Produce failed (not enough resources or no base)"));
                    send_json(players[pid].sockfd, jerr);
                    json_object_put(jerr);
                }
                break;
            }
            case CMD_MOVE_UNIT: {
                struct json_object *ju   = NULL;
                struct json_object *jnx  = NULL;
                struct json_object *jny  = NULL;
                json_object_object_get_ex(jpl, "unit_id", &ju);
                json_object_object_get_ex(jpl, "x", &jnx);
                json_object_object_get_ex(jpl, "y", &jny);
                int uid = json_object_get_int(ju);
                int nx  = json_object_get_int(jnx);
                int ny  = json_object_get_int(jny);
                result = move_unit(&gstate, pid, uid, nx, ny);
                if (result < 0) {
                    struct json_object *jerr = json_object_new_object();
                    json_object_object_add(jerr, "action",
                                           json_object_new_string(ACTION_ERROR));
                    json_object_object_add(
                        jerr, "message",
                        json_object_new_string(
                            "Move failed (no such unit or out of range)"));
                    send_json(players[pid].sockfd, jerr);
                    json_object_put(jerr);
                }
                break;
            }
            case CMD_ATTACK_UNIT: {
                struct json_object *ju  = NULL;
                struct json_object *jt  = NULL;
                json_object_object_get_ex(jpl, "attacker_id", &ju);
                json_object_object_get_ex(jpl, "target_id", &jt);
                int aid = json_object_get_int(ju);
                int tid = json_object_get_int(jt);
                result = attack_unit(&gstate, pid, aid, tid);
                if (result < 0) {
                    struct json_object *jerr = json_object_new_object();
                    json_object_object_add(jerr, "action",
                                           json_object_new_string(ACTION_ERROR));
                    json_object_object_add(
                        jerr, "message",
                        json_object_new_string(
                            "Attack failed (no such unit or out of range)"));
                    send_json(players[pid].sockfd, jerr);
                    json_object_put(jerr);
                }
                break;
            }
            case CMD_REQUEST_STATE: {
                // “state” 요청이 왔을 때, 현재 상태를 해당 클라이언트에만 전송
                struct json_object *jstate = game_state_to_json(&gstate);
                struct json_object *jres   = json_object_new_object();
                json_object_object_add(jres, "action",
                                      json_object_new_string(ACTION_UPDATE_STATE));
                json_object_object_add(jres, "state", jstate);
                send_json(players[pid].sockfd, jres);
                json_object_put(jres);
                break;
            }
            default:
                // 알 수 없는 cmd → 무시
                break;
        }
    }

    json_object_put(jmsg);
}

// ──────────────────────────────────────────────────────────
// 주기적으로 모든 클라이언트에게 현재 전체 게임 상태를 전송
// ──────────────────────────────────────────────────────────
void broadcast_state() {
    struct json_object *jstate = game_state_to_json(&gstate);
    struct json_object *jmsg   = json_object_new_object();
    json_object_object_add(jmsg, "action",
                          json_object_new_string(ACTION_UPDATE_STATE));
    json_object_object_add(jmsg, "state", jstate);

    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (players[i].connected) {
            send_json(players[i].sockfd, jmsg);
        }
    }
    json_object_put(jmsg);
}

// 게임 오버 브로드캐스트 함수
void broadcast_game_over() {
    struct json_object *jmsg = json_object_new_object();
    json_object_object_add(jmsg, "action", json_object_new_string(ACTION_GAME_OVER));
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (players[i].connected) {
            send_json(players[i].sockfd, jmsg);
        }
    }
    json_object_put(jmsg);
}

// ──────────────────────────────────────────────────────────
// 서버 메인 함수
// ──────────────────────────────────────────────────────────
int main(int argc, char *argv[]) {
    if (argc != 2) {
        printf("Usage: %s <Port>\n", argv[0]);
        return 1;
    }
    int port = atoi(argv[1]);
    srand(time(NULL));

    // 1) players 초기화
    for (int i = 0; i < MAX_CLIENTS; i++) {
        players[i].connected = 0;
    }
    // 2) 게임 상태 초기화
    init_game_state(&gstate);

    // 3) 리슨 소켓 생성
    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) {
        perror("socket");
        return 1;
    }
    int opt = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family      = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_port        = htons(port);
    if (bind(listen_fd, (struct sockaddr *)&serv_addr,
             sizeof(serv_addr)) < 0) {
        perror("bind");
        return 1;
    }
    if (listen(listen_fd, MAX_CLIENTS) < 0) {
        perror("listen");
        return 1;
    }
    printf("[Server] Listening on port %d\n", port);

    // 4) select() 설정
    fd_set master_set, read_set;
    int max_fd = listen_fd;
    FD_ZERO(&master_set);
    FD_SET(listen_fd, &master_set);

    // 5) 메인 루프
    struct timeval tv;
    while (1) {
        read_set = master_set;
        // 타임아웃: 1초
        tv.tv_sec  = 1;
        tv.tv_usec = 0;

        int activity = select(max_fd + 1, &read_set, NULL, NULL, &tv);
        if (activity < 0 && errno != EINTR) {
            perror("select");
            break;
        }

        // 타임아웃 시(=activity == 0) → 게임 상태 업데이트, 브로드캐스트
        if (activity == 0) {
            update_game(&gstate);

            // 게임 오버 감지: 반드시 2명 이상 기지 배치 후 한 명만 남았을 때만
            int alive_count = 0;
            for (int i = 0; i < MAX_CLIENTS; i++) {
                if (gstate.players[i].base_x != -1 && !is_base_destroyed(&gstate, i)) {
                    alive_count++;
                }
            }
            if (alive_count >= 2 && gstate.event_flag == 99) {
                broadcast_game_over();
                break; // 서버 종료
            }

            broadcast_state();
            continue;
        }

        // 읽기 가능한 fd 확인
        for (int fd = 0; fd <= max_fd; fd++) {
            if (!FD_ISSET(fd, &read_set))
                continue;

            if (fd == listen_fd) {
                // 새로운 연결
                handle_new_connection(listen_fd);
                // 새로 열린 소켓을 master_set에 추가
                for (int i = 0; i < MAX_CLIENTS; i++) {
                    if (players[i].connected) {
                        FD_SET(players[i].sockfd, &master_set);
                        if (players[i].sockfd > max_fd) {
                            max_fd = players[i].sockfd;
                        }
                    }
                }
            } else {
                // 기존 클라이언트 메시지 처리
                int pid = -1;
                for (int i = 0; i < MAX_CLIENTS; i++) {
                    if (players[i].connected && players[i].sockfd == fd) {
                        pid = i;
                        break;
                    }
                }
                if (pid >= 0) {
                    handle_client_msg(pid, &master_set); // master_set 전달
                }
            }
        }
    }

    close(listen_fd);
    return 0;
}
