/**
 * baseball_server.c - 숫자 야구 네트워크 게임 서버
 * 
 * 📋 주요 기능:
 * - TCP 소켓 기반 1:1 실시간 대전 게임 서버
 * - select() I/O 멀티플렉싱으로 다중 클라이언트 처리
 * - JSON 프로토콜 기반 구조화된 통신
 * - 턴제 게임 로직 및 상태 동기화
 * - 네트워크 지연 및 연결 안정성 처리
 * 
 * 🔧 기술적 특징:
 * - 비동기 I/O: select()로 블로킹 없는 통신
 * - 상태 관리: GameManager를 통한 중앙집중식 게임 상태 관리
 * - 에러 처리: 연결 해제, 타임아웃, 프로토콜 오류 처리
 * - 메시지 프로토콜: 길이 prefix + JSON 페이로드 방식
 * 
 * 네트워크 프로그래밍 과제용 - 고급 TCP 서버 구현
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <arpa/inet.h>
#include <time.h>

#include "baseball_protocol.h"

// ──────────────────────────────────────────────────────────
// 전역 변수
// ──────────────────────────────────────────────────────────
GameManager game;           // 전역 게임 상태 관리자
time_t last_heartbeat_check; // 마지막 하트비트 체크 시간

// ──────────────────────────────────────────────────────────
// 함수 전방 선언 (Forward Declarations)
// ──────────────────────────────────────────────────────────
void start_turn(void);                          // 게임 턴 시작 처리
void check_all_numbers_set(void);               // 모든 플레이어 숫자 설정 완료 확인
void check_player_timeouts(fd_set *master_set); // 플레이어 타임아웃 체크 (새로 추가)
void send_heartbeat_to_all(void);               // 모든 플레이어에게 하트비트 전송 (새로 추가)
void cleanup_disconnected_player(int player_id, fd_set *master_set); // 연결 해제 정리 (새로 추가)

// ──────────────────────────────────────────────────────────
// JSON 송수신 함수들 (Network Communication Layer)
// ──────────────────────────────────────────────────────────

/**
 * JSON 객체를 소켓으로 전송
 * 프로토콜: [2바이트 길이] + [JSON 문자열]
 * 
 * @param fd: 소켓 파일 디스크립터
 * @param jobj: 전송할 JSON 객체
 * @return: 성공 시 0, 실패 시 -1
 */
int send_json(int fd, struct json_object *jobj) {
    const char *s = json_object_to_json_string(jobj);
    int len = strlen(s);
    uint16_t netlen = htons(len);  // 네트워크 바이트 순서로 변환
    
    // 1단계: 메시지 길이 전송 (2바이트)
    if (send(fd, &netlen, sizeof(netlen), 0) != sizeof(netlen)) {
        printf("[Server] 메시지 길이 전송 실패 (fd=%d): %s\n", fd, strerror(errno));
        return -1;
    }
    
    // 2단계: JSON 문자열 전송
    if (send(fd, s, len, 0) != len) {
        printf("[Server] JSON 데이터 전송 실패 (fd=%d): %s\n", fd, strerror(errno));
        return -1;
    }
    
    return 0;
}

/**
 * 소켓에서 JSON 객체 수신
 * 타임아웃과 부분 수신 처리를 포함한 안전한 수신
 * 
 * @param fd: 소켓 파일 디스크립터
 * @return: 수신된 JSON 객체 포인터, 실패 시 NULL
 */
struct json_object *recv_json(int fd) {
    uint16_t netlen;
    
    // 1단계: 메시지 길이 수신 (2바이트, 완전 수신까지 대기)
    ssize_t n = recv(fd, &netlen, sizeof(netlen), MSG_WAITALL);
    if (n <= 0) {
        if (n == 0) {
            printf("[Server] 클라이언트가 연결을 종료했습니다 (fd=%d)\n", fd);
        } else {
            printf("[Server] 메시지 길이 수신 실패 (fd=%d): %s\n", fd, strerror(errno));
        }
        return NULL;
    }
    
    int len = ntohs(netlen);  // 호스트 바이트 순서로 변환
    
    // 길이 유효성 검사 (DoS 공격 방지)
    if (len <= 0 || len > BUF_SIZE) {
        printf("[Server] 잘못된 메시지 길이: %d bytes (fd=%d)\n", len, fd);
        return NULL;
    }
    
    // 2단계: JSON 문자열 수신
    char buf[BUF_SIZE + 1];
    n = recv(fd, buf, len, MSG_WAITALL);
    if (n <= 0) {
        printf("[Server] JSON 데이터 수신 실패 (fd=%d): %s\n", fd, strerror(errno));
        return NULL;
    }
    
    buf[len] = '\0';  // NULL terminator 추가
    
    // 3단계: JSON 파싱
    struct json_object *jobj = json_tokener_parse(buf);
    if (jobj == NULL) {
        printf("[Server] JSON 파싱 실패 (fd=%d): %s\n", fd, buf);
        return NULL;
    }
    
    return jobj;
}

// ──────────────────────────────────────────────────────────
// 게임 초기화 및 관리 함수들 (Game Management Layer)
// ──────────────────────────────────────────────────────────

/**
 * 게임 매니저 초기화
 * 모든 플레이어 상태를 기본값으로 설정하고 게임 준비
 */
void init_game() {
    // 게임 전체 상태 초기화
    game.state = GAME_WAITING;
    game.current_turn = 0;
    game.players_ready = 0;
    game.game_start_time = time(NULL);
    game.last_heartbeat = time(NULL);
    
    // 모든 플레이어 정보 초기화
    for (int i = 0; i < MAX_CLIENTS; i++) {
        game.players[i].sockfd = -1;
        game.players[i].player_id = i;
        game.players[i].connected = 0;
        game.players[i].state = PLAYER_WAITING;
        memset(game.players[i].secret_number, 0, 4);
        game.players[i].attempts = 0;
        game.players[i].is_winner = 0;
        game.players[i].last_activity = time(NULL);  // 네트워크 지연 처리용
        game.players[i].retry_count = 0;             // 재시도 횟수 초기화
    }
    
    // 전역 하트비트 타이머 초기화
    last_heartbeat_check = time(NULL);
    
    printf("[Server] 숫자 야구 게임 서버 초기화 완료\n");
    printf("[Server] 네트워크 타임아웃: %d초, 하트비트 간격: %d초\n", 
           NETWORK_TIMEOUT_SEC, HEARTBEAT_INTERVAL_SEC);
}

// ──────────────────────────────────────────────────────────
// 네트워크 지연 및 안정성 처리 함수들 (Network Reliability Layer)
// ──────────────────────────────────────────────────────────

/**
 * 모든 플레이어의 타임아웃 상태 체크
 * 30초 이상 비활성 플레이어는 연결 해제 처리
 * 
 * @param master_set: select()용 파일 디스크립터 집합
 */
void check_player_timeouts(fd_set *master_set) {
    time_t current_time = time(NULL);
    
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (!game.players[i].connected) continue;
        
        // 타임아웃 체크 (30초 이상 비활성)
        if (is_player_timeout(&game.players[i])) {
            printf("[Server] 플레이어 %d 타임아웃 - 연결을 해제합니다\n", i);
            
            // 상대방에게 타임아웃 알림
            int opponent_id = 1 - i;
            if (game.players[opponent_id].connected) {
                struct json_object *timeout_msg = create_timeout_message("상대방이 연결을 잃었습니다");
                send_json(game.players[opponent_id].sockfd, timeout_msg);
                json_object_put(timeout_msg);
            }
            
            // 연결 정리
            cleanup_disconnected_player(i, master_set);
        }
    }
}

/**
 * 모든 연결된 플레이어에게 하트비트 메시지 전송
 * 10초마다 연결 상태 확인용 메시지 전송
 */
void send_heartbeat_to_all(void) {
    time_t current_time = time(NULL);
    
    // 하트비트 간격 체크 (10초마다)
    if (current_time - last_heartbeat_check < HEARTBEAT_INTERVAL_SEC) {
        return;
    }
    
    // 모든 연결된 플레이어에게 하트비트 전송
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (game.players[i].connected) {
            struct json_object *heartbeat = create_heartbeat_message();
            if (send_json(game.players[i].sockfd, heartbeat) < 0) {
                printf("[Server] 플레이어 %d 하트비트 전송 실패\n", i);
            }
            json_object_put(heartbeat);
        }
    }
    
    last_heartbeat_check = current_time;
    printf("[Server] 하트비트 전송 완료\n");
}

/**
 * 연결 해제된 플레이어 정리
 * 소켓 닫기, 상태 초기화, select() 집합에서 제거
 * 
 * @param player_id: 정리할 플레이어 ID
 * @param master_set: select()용 파일 디스크립터 집합
 */
void cleanup_disconnected_player(int player_id, fd_set *master_set) {
    if (player_id < 0 || player_id >= MAX_CLIENTS) return;
    
    PlayerInfo *player = &game.players[player_id];
    
    if (player->sockfd >= 0) {
        // select() 집합에서 제거
        FD_CLR(player->sockfd, master_set);
        
        // 소켓 종료
        close(player->sockfd);
        printf("[Server] 플레이어 %d 소켓 종료 (fd=%d)\n", player_id, player->sockfd);
    }
    
    // 플레이어 상태 초기화
    player->sockfd = -1;
    player->connected = 0;
    player->state = PLAYER_WAITING;
    memset(player->secret_number, 0, 4);
    player->attempts = 0;
    player->is_winner = 0;
    player->retry_count = 0;
    
    // 게임 상태 조정
    game.players_ready--;
    if (game.players_ready < 0) game.players_ready = 0;
    
    // 게임 중이었다면 게임 종료
    if (game.state == GAME_PLAYING || game.state == GAME_SETTING) {
        game.state = GAME_WAITING;
        printf("[Server] 플레이어 연결 해제로 인한 게임 종료\n");
    }
}

// ──────────────────────────────────────────────────────────
// 메시징 및 브로드캐스트 함수들 (Messaging Layer)
// ──────────────────────────────────────────────────────────

/**
 * 모든 연결된 플레이어에게 메시지 브로드캐스트
 * 게임 상태 변경, 공지사항 등 전체 알림용
 * 
 * @param jmsg: 브로드캐스트할 JSON 메시지
 */
void broadcast_to_all(struct json_object *jmsg) {
    int sent_count = 0;
    
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (game.players[i].connected) {
            if (send_json(game.players[i].sockfd, jmsg) == 0) {
                sent_count++;
                update_player_activity(&game.players[i]); // 활동 시간 업데이트
            } else {
                printf("[Server] 플레이어 %d에게 브로드캐스트 실패\n", i);
            }
        }
    }
    
    printf("[Server] 브로드캐스트 완료: %d명에게 전송\n", sent_count);
}

/**
 * 특정 플레이어에게 개별 메시지 전송
 * 턴 알림, 개인별 게임 결과 등 개별 통신용
 * 
 * @param player_id: 대상 플레이어 ID (0 또는 1)
 * @param jmsg: 전송할 JSON 메시지
 */
void send_to_player(int player_id, struct json_object *jmsg) {
    // 유효성 검사
    if (player_id < 0 || player_id >= MAX_CLIENTS) {
        printf("[Server] 잘못된 플레이어 ID: %d\n", player_id);
        return;
    }
    
    if (!game.players[player_id].connected) {
        printf("[Server] 플레이어 %d는 연결되어 있지 않습니다\n", player_id);
        return;
    }
    
    // 메시지 전송
    if (send_json(game.players[player_id].sockfd, jmsg) == 0) {
        update_player_activity(&game.players[player_id]); // 활동 시간 업데이트
        printf("[Server] 플레이어 %d에게 메시지 전송 완료\n", player_id);
    } else {
        printf("[Server] 플레이어 %d에게 메시지 전송 실패\n", player_id);
        
        // 전송 실패 시 재시도 카운터 증가
        game.players[player_id].retry_count++;
        if (game.players[player_id].retry_count >= MAX_RETRY_COUNT) {
            printf("[Server] 플레이어 %d 최대 재시도 횟수 초과 - 연결 해제\n", player_id);
            // 연결 해제는 메인 루프에서 처리하도록 플래그만 설정
            game.players[player_id].connected = 0;
        }
    }
}

// ──────────────────────────────────────────────────────────
// 게임 초기화
// ──────────────────────────────────────────────────────────
void start_game() {
    if (game.state != GAME_WAITING || game.players_ready < 2) return;
    
    game.state = GAME_SETTING;
    printf("[Server] 게임 시작! 플레이어들이 숫자를 설정하세요.\n");
    
    // 모든 플레이어에게 게임 시작 알림
    struct json_object *jmsg = create_message(ACTION_GAME_START);
    json_object_object_add(jmsg, "message", 
        json_object_new_string("게임이 시작되었습니다! 3자리 숫자를 설정하세요."));
    broadcast_to_all(jmsg);
    json_object_put(jmsg);
    
    // 각 플레이어 상태를 설정 중으로 변경
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (game.players[i].connected) {
            game.players[i].state = PLAYER_SETTING;
        }
    }
}

// ──────────────────────────────────────────────────────────
// 숫자 설정 완료 확인 및 게임 진행 시작
// ──────────────────────────────────────────────────────────
void check_all_numbers_set() {
    int ready_count = 0;
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (game.players[i].connected && game.players[i].state == PLAYER_READY) {
            ready_count++;
        }
    }
    
    if (ready_count == 2) {
        game.state = GAME_PLAYING;
        game.current_turn = 0;  // 첫 번째 플레이어부터 시작
        
        printf("[Server] 모든 플레이어가 숫자를 설정했습니다. 게임을 시작합니다!\n");
        
        // 턴 알림
        start_turn();
    }
}

// ──────────────────────────────────────────────────────────
// 턴 시작 처리
// ──────────────────────────────────────────────────────────
void start_turn() {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (!game.players[i].connected) continue;
        
        struct json_object *jmsg;
        if (i == game.current_turn) {
            // 현재 턴 플레이어
            jmsg = create_message(ACTION_YOUR_TURN);
            json_object_object_add(jmsg, "message", 
                json_object_new_string("당신의 턴입니다! 3자리 숫자를 추측하세요."));
            game.players[i].state = PLAYER_TURN;
        } else {
            // 대기 중인 플레이어
            jmsg = create_message(ACTION_WAIT_TURN);
            json_object_object_add(jmsg, "message", 
                json_object_new_string("상대방의 턴입니다. 잠시 기다려주세요."));
            game.players[i].state = PLAYER_WAITING_TURN;
        }
        
        send_to_player(i, jmsg);
        json_object_put(jmsg);
    }
}

// ──────────────────────────────────────────────────────────
// 게임 종료 처리
// ──────────────────────────────────────────────────────────
void end_game(int winner_id) {
    game.state = GAME_FINISHED;
    
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (!game.players[i].connected) continue;
        
        struct json_object *jmsg = create_message(ACTION_GAME_OVER);
        
        if (i == winner_id) {
            json_object_object_add(jmsg, "result", json_object_new_string("victory"));
            json_object_object_add(jmsg, "message", 
                json_object_new_string("🎉 축하합니다! 숫자를 맞추셨습니다!"));
        } else {
            json_object_object_add(jmsg, "result", json_object_new_string("defeat"));
            json_object_object_add(jmsg, "message", 
                json_object_new_string("😢 아쉽네요! 상대방이 먼저 맞췄습니다."));
        }
        
        // 정답 공개
        json_object_object_add(jmsg, "your_number", 
            json_object_new_string(game.players[i].secret_number));
        json_object_object_add(jmsg, "opponent_number", 
            json_object_new_string(game.players[1-i].secret_number));
        
        send_to_player(i, jmsg);
        json_object_put(jmsg);
    }
    
    printf("[Server] 게임 종료! 플레이어 %d 승리\n", winner_id);
    
    // 5초 후 게임 상태를 대기 상태로 초기화
    printf("[Server] 5초 후 새 게임 준비...\n");
    sleep(5);
    
    // 게임 상태 초기화 (플레이어 연결은 유지)
    game.state = GAME_WAITING;
    game.current_turn = 0;
    
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (game.players[i].connected) {
            game.players[i].state = PLAYER_WAITING;
            memset(game.players[i].secret_number, 0, 4);
            game.players[i].attempts = 0;
            game.players[i].is_winner = 0;
        }
    }
    
    printf("[Server] 새 게임 준비 완료 - 플레이어들이 새 게임을 시작할 수 있습니다!\n");
}

// ──────────────────────────────────────────────────────────
// 새로운 연결 처리
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
    int player_id = -1;
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (!game.players[i].connected) {
            player_id = i;
            break;
        }
    }
    
    if (player_id < 0) {
        // 현재 게임이 진행 중이라면 정중하게 알림
        if (game.state == GAME_PLAYING || game.state == GAME_SETTING) {
            struct json_object *jerr = create_error("현재 게임이 진행 중입니다. 잠시 후 다시 시도해주세요.");
            send_json(conn_fd, jerr);
            json_object_put(jerr);
            printf("[Server] 게임 진행 중 - 새 플레이어 연결 거부 (IP: %s)\n", 
                   inet_ntoa(cli_addr.sin_addr));
        } else {
            struct json_object *jerr = create_error("서버에 접속할 수 없습니다. 나중에 다시 시도해주세요.");
            send_json(conn_fd, jerr);
            json_object_put(jerr);
            printf("[Server] 서버 용량 초과 - 연결 거부 (IP: %s)\n", 
                   inet_ntoa(cli_addr.sin_addr));
        }
        close(conn_fd);
        return;
    }
    
    // 플레이어 등록
    game.players[player_id].sockfd = conn_fd;
    game.players[player_id].connected = 1;
    game.players[player_id].state = PLAYER_WAITING;
    game.players_ready++;
    
    // 플레이어 ID 할당 메시지
    struct json_object *jmsg = create_message(ACTION_ASSIGN_ID);
    json_object_object_add(jmsg, "player_id", json_object_new_int(player_id));
    send_to_player(player_id, jmsg);
    json_object_put(jmsg);
    
    printf("[Server] 플레이어 %d 연결됨 (IP: %s)\n", 
           player_id, inet_ntoa(cli_addr.sin_addr));
    
    if (game.players_ready == 2) {
        start_game();
    } else {
        // 상대방 대기 중 메시지
        struct json_object *wait_msg = create_message(ACTION_WAIT_PLAYER);
        json_object_object_add(wait_msg, "message", 
            json_object_new_string("상대방을 기다리고 있습니다..."));
        send_to_player(player_id, wait_msg);
        json_object_put(wait_msg);
    }
}

// ──────────────────────────────────────────────────────────
// 클라이언트 메시지 처리
// ──────────────────────────────────────────────────────────
void handle_client_message(int player_id, fd_set *master_set) {
    struct json_object *jmsg = recv_json(game.players[player_id].sockfd);
    
    if (!jmsg) {
        // 연결 종료
        printf("[Server] 플레이어 %d 연결 해제\n", player_id);
        close(game.players[player_id].sockfd);
        FD_CLR(game.players[player_id].sockfd, master_set);
        game.players[player_id].connected = 0;
        game.players_ready--;
        
        // 게임 중이었다면 상대방에게 승리 메시지
        if (game.state == GAME_PLAYING || game.state == GAME_SETTING) {
            int other_player = 1 - player_id;
            if (game.players[other_player].connected) {
                struct json_object *win_msg = create_message(ACTION_GAME_OVER);
                json_object_object_add(win_msg, "result", json_object_new_string("victory"));
                json_object_object_add(win_msg, "message", 
                    json_object_new_string("🎉 상대방이 나갔습니다. 당신의 승리!"));
                send_to_player(other_player, win_msg);
                json_object_put(win_msg);
            }
        }
        return;
    }
    
    // action 필드 확인
    struct json_object *jact = NULL;
    if (!json_object_object_get_ex(jmsg, "action", &jact)) {
        json_object_put(jmsg);
        return;
    }
    
    const char *action = json_object_get_string(jact);
    
    // 숫자 설정 처리
    if (strcmp(action, ACTION_SET_NUMBER) == 0) {
        if (game.players[player_id].state != PLAYER_SETTING) {
            struct json_object *jerr = create_error("지금은 숫자를 설정할 수 없습니다.");
            send_to_player(player_id, jerr);
            json_object_put(jerr);
            json_object_put(jmsg);
            return;
        }
        
        struct json_object *jnum = NULL;
        if (json_object_object_get_ex(jmsg, "number", &jnum)) {
            const char *number = json_object_get_string(jnum);
            
            if (is_valid_number(number)) {
                strcpy(game.players[player_id].secret_number, number);
                game.players[player_id].state = PLAYER_READY;
                
                struct json_object *jresp = create_message(ACTION_NUMBER_SET);
                json_object_object_add(jresp, "message", 
                    json_object_new_string("숫자가 설정되었습니다. 상대방을 기다리는 중..."));
                send_to_player(player_id, jresp);
                json_object_put(jresp);
                
                printf("[Server] 플레이어 %d가 숫자를 설정했습니다.\n", player_id);
                check_all_numbers_set();
            } else {
                struct json_object *jerr = create_error("올바르지 않은 숫자입니다. 3자리 서로 다른 숫자를 입력하세요.");
                send_to_player(player_id, jerr);
                json_object_put(jerr);
            }
        }
    }
    // 추측 처리
    else if (strcmp(action, ACTION_GUESS) == 0) {
        if (game.players[player_id].state != PLAYER_TURN) {
            struct json_object *jerr = create_error("지금은 당신의 턴이 아닙니다.");
            send_to_player(player_id, jerr);
            json_object_put(jerr);
            json_object_put(jmsg);
            return;
        }
        
        struct json_object *jguess = NULL;
        if (json_object_object_get_ex(jmsg, "guess", &jguess)) {
            const char *guess = json_object_get_string(jguess);
            
            if (is_valid_number(guess)) {
                int opponent_id = 1 - player_id;
                GuessResult result = calculate_result(
                    game.players[opponent_id].secret_number, guess);
                
                game.players[player_id].attempts++;
                
                // 결과 메시지 생성
                struct json_object *jresult = create_message(ACTION_GUESS_RESULT);
                json_object_object_add(jresult, "guess", json_object_new_string(guess));
                json_object_object_add(jresult, "strikes", json_object_new_int(result.strikes));
                json_object_object_add(jresult, "balls", json_object_new_int(result.balls));
                json_object_object_add(jresult, "attempts", json_object_new_int(game.players[player_id].attempts));
                json_object_object_add(jresult, "current_player", json_object_new_int(player_id));
                
                // 양쪽 플레이어에게 결과 전송
                broadcast_to_all(jresult);
                json_object_put(jresult);
                
                printf("[Server] 플레이어 %d 추측: %s -> %dS %dB\n", 
                       player_id, guess, result.strikes, result.balls);
                
                if (result.is_correct) {
                    // 게임 종료
                    end_game(player_id);
                } else {
                    // 턴 변경
                    game.current_turn = 1 - game.current_turn;
                    start_turn();
                }
            } else {
                struct json_object *jerr = create_error("올바르지 않은 숫자입니다. 3자리 서로 다른 숫자를 입력하세요.");
                send_to_player(player_id, jerr);
                json_object_put(jerr);
            }
        }
    }
    
    json_object_put(jmsg);
}

// ──────────────────────────────────────────────────────────
// 메인 함수
// ──────────────────────────────────────────────────────────
int main(int argc, char *argv[]) {
    if (argc != 2) {
        printf("사용법: %s <포트>\n", argv[0]);
        return 1;
    }
    
    int port = atoi(argv[1]);
    
    // 게임 초기화
    init_game();
    
    // 소켓 생성
    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) {
        perror("socket");
        return 1;
    }
    
    int opt = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_port = htons(port);
    
    if (bind(listen_fd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("bind");
        return 1;
    }
    
    if (listen(listen_fd, MAX_CLIENTS) < 0) {
        perror("listen");
        return 1;
    }
    
    printf("[Server] 숫자 야구 서버가 포트 %d에서 시작되었습니다.\n", port);
    printf("[Server] 플레이어 2명을 기다리는 중...\n");
    
    // select() 설정
    fd_set master_set, read_set;
    int max_fd = listen_fd;
    FD_ZERO(&master_set);
    FD_SET(listen_fd, &master_set);
    
    // 메인 루프
    while (1) {
        read_set = master_set;
        
        int activity = select(max_fd + 1, &read_set, NULL, NULL, NULL);
        if (activity < 0 && errno != EINTR) {
            perror("select");
            break;
        }
        
        // 읽기 가능한 fd 확인
        for (int fd = 0; fd <= max_fd; fd++) {
            if (!FD_ISSET(fd, &read_set)) continue;
            
            if (fd == listen_fd) {
                // 새로운 연결
                handle_new_connection(listen_fd);
                
                // 새로 열린 소켓을 master_set에 추가
                for (int i = 0; i < MAX_CLIENTS; i++) {
                    if (game.players[i].connected) {
                        FD_SET(game.players[i].sockfd, &master_set);
                        if (game.players[i].sockfd > max_fd) {
                            max_fd = game.players[i].sockfd;
                        }
                    }
                }
            } else {
                // 기존 클라이언트 메시지 처리
                int player_id = -1;
                for (int i = 0; i < MAX_CLIENTS; i++) {
                    if (game.players[i].connected && game.players[i].sockfd == fd) {
                        player_id = i;
                        break;
                    }
                }
                
                if (player_id >= 0) {
                    handle_client_message(player_id, &master_set);
                }
            }
        }
    }
    
    close(listen_fd);
    return 0;
} 