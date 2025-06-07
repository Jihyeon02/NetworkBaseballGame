/**
 * baseball_client.c - 숫자 야구 네트워크 게임 클라이언트
 * 
 * 📋 주요 기능:
 * - TCP 소켓을 통한 게임 서버 연결
 * - 실시간 양방향 통신으로 게임 진행
 * - 풍부한 Unicode UI/UX 제공
 * - 사용자 입력 처리 및 명령어 파싱
 * - 게임 상태 시각화 및 결과 표시
 * 
 * 🎨 UI/UX 특징:
 * - 애니메이션 배너와 색상 표시
 * - 실시간 게임 상태 업데이트
 * - 직관적인 명령어 시스템
 * - 상세한 결과 시각화 (스트라이크/볼)
 * - 승리/패배 화면 연출
 * 
 * 🔧 기술적 특징:
 * - 비동기 메시지 수신 처리
 * - JSON 프로토콜 기반 통신
 * - 에러 처리 및 사용자 피드백
 * - 명령어 검증 및 입력 파싱
 * 
 * 네트워크 프로그래밍 과제용 - 고급 TCP 클라이언트 구현
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#include "baseball_protocol.h"

// ──────────────────────────────────────────────────────────
// 클라이언트 게임 상태 전역 변수
// ──────────────────────────────────────────────────────────
int my_player_id = -1;      // 서버에서 할당받은 플레이어 ID (0 또는 1)
int game_started = 0;       // 게임 시작 여부 (0: 대기중, 1: 시작됨)
int number_set = 0;         // 내 숫자 설정 완료 여부 (0: 미설정, 1: 설정완료)
int my_turn = 0;            // 현재 내 턴 여부 (0: 상대턴, 1: 내턴)

// ──────────────────────────────────────────────────────────
// JSON 송수신 함수들 (Network Communication Layer)
// 서버와 동일한 프로토콜 사용: [2바이트 길이] + [JSON 문자열]
// ──────────────────────────────────────────────────────────

/**
 * JSON 객체를 서버로 전송
 * 서버와 동일한 프로토콜 사용
 * 
 * @param fd: 서버 소켓 파일 디스크립터
 * @param jobj: 전송할 JSON 객체
 * @return: 성공 시 0, 실패 시 -1
 */
int send_json(int fd, struct json_object *jobj) {
    const char *s = json_object_to_json_string(jobj);
    int len = strlen(s);
    uint16_t netlen = htons(len);  // 네트워크 바이트 순서로 변환
    
    // 1단계: 메시지 길이 전송 (2바이트)
    if (send(fd, &netlen, sizeof(netlen), 0) != sizeof(netlen)) {
        printf("🚨 서버 통신 오류: 메시지 길이 전송 실패\n");
        return -1;
    }
    
    // 2단계: JSON 문자열 전송
    if (send(fd, s, len, 0) != len) {
        printf("🚨 서버 통신 오류: JSON 데이터 전송 실패\n");
        return -1;
    }
    
    return 0;
}

/**
 * 서버에서 JSON 객체 수신
 * 타임아웃과 부분 수신 처리 포함
 * 
 * @param fd: 서버 소켓 파일 디스크립터
 * @return: 수신된 JSON 객체 포인터, 실패 시 NULL
 */
struct json_object *recv_json(int fd) {
    uint16_t netlen;
    
    // 1단계: 메시지 길이 수신 (2바이트, 완전 수신까지 대기)
    ssize_t n = recv(fd, &netlen, sizeof(netlen), MSG_WAITALL);
    if (n <= 0) {
        if (n == 0) {
            printf("🔌 서버가 연결을 종료했습니다\n");
        } else {
            printf("🚨 네트워크 오류: 메시지 길이 수신 실패\n");
        }
        return NULL;
    }
    
    int len = ntohs(netlen);  // 호스트 바이트 순서로 변환
    
    // 길이 유효성 검사
    if (len <= 0 || len > BUF_SIZE) {
        printf("🚨 프로토콜 오류: 잘못된 메시지 길이 (%d bytes)\n", len);
        return NULL;
    }
    
    // 2단계: JSON 문자열 수신
    char buf[BUF_SIZE + 1];
    n = recv(fd, buf, len, MSG_WAITALL);
    if (n <= 0) {
        printf("🚨 네트워크 오류: JSON 데이터 수신 실패\n");
        return NULL;
    }
    
    buf[len] = '\0';  // NULL terminator 추가
    
    // 3단계: JSON 파싱
    struct json_object *jobj = json_tokener_parse(buf);
    if (jobj == NULL) {
        printf("🚨 프로토콜 오류: JSON 파싱 실패\n");
        return NULL;
    }
    
    return jobj;
}

// ──────────────────────────────────────────────────────────
// 화면 제어 및 UI 함수들 (User Interface Layer)
// Unicode 문자와 박스 그래픽을 활용한 풍부한 사용자 경험 제공
// ──────────────────────────────────────────────────────────

/**
 * 터미널 화면 완전 초기화
 * ANSI 이스케이프 시퀀스를 사용한 화면 클리어
 */
void clear_screen() {
    printf("\033[2J\033[H");  // 화면 클리어 및 커서 홈으로 이동
}

/**
 * 게임 시작 시 애니메이션 배너 출력
 * 야구 이모지와 박스 그래픽으로 시각적 임팩트 제공
 */
void print_animated_banner() {
    printf("\n");
    printf("    ⚾⚾⚾⚾⚾⚾⚾⚾⚾⚾⚾⚾⚾⚾⚾⚾⚾⚾⚾⚾⚾⚾⚾⚾⚾⚾⚾⚾⚾⚾\n");
    printf("   ⚾                                                    ⚾\n");
    printf("  ⚾     🎯 ✨ 숫자 야구 네트워크 게임 ✨ 🎯           ⚾\n");
    printf(" ⚾                                                      ⚾\n");
    printf("⚾        🔥 REAL-TIME NETWORK BASEBALL GAME 🔥          ⚾\n");
    printf(" ⚾                                                      ⚾\n");
    printf("  ⚾     ⭐ 1 vs 1 온라인 대전 ⭐                      ⚾\n");
    printf("   ⚾                                                    ⚾\n");
    printf("    ⚾⚾⚾⚾⚾⚾⚾⚾⚾⚾⚾⚾⚾⚾⚾⚾⚾⚾⚾⚾⚾⚾⚾⚾⚾⚾⚾⚾⚾⚾\n");
    printf("\n");
    fflush(stdout);
}

/**
 * 서버 연결 시 환영 화면 출력
 * 로딩 바와 연결 상태를 시각적으로 표현
 */
void print_welcome_screen() {
    clear_screen();
    print_animated_banner();
    
    printf("╭─────────────────────────────────────────────────────────────╮\n");
    printf("│  🌟 환영합니다! Welcome to Baseball Network Game! 🌟        │\n");
    printf("├─────────────────────────────────────────────────────────────┤\n");
    printf("│                                                             │\n");
    printf("│  🎮 서버에 연결 중... 잠시만 기다려주세요!                    │\n");
    printf("│                                                             │\n");
    printf("│  💫 Connection Status: [████████████████████] 100%%         │\n");
    printf("│                                                             │\n");
    printf("╰─────────────────────────────────────────────────────────────╯\n");
    printf("\n");
    fflush(stdout);
    usleep(500000); // 0.5초 대기 (사용자 경험 향상)
}

/**
 * 게임 메인 헤더 출력
 * 일관된 게임 브랜딩과 시각적 정체성 제공
 */
void print_game_header() {
    printf("\n");
    printf("╔═══════════════════════════════════════════════════════════════╗\n");
    printf("║  🎯 ⚾ 숫자 야구 네트워크 게임 ⚾ 🎯                             ║\n");
    printf("║                                                               ║\n");
    printf("║  🔥 실시간 1:1 대전 🔥    💎 3자리 숫자 맞추기 💎              ║\n");
    printf("╚═══════════════════════════════════════════════════════════════╝\n");
    printf("\n");
}

/**
 * 플레이어 상태 정보 표시
 * ID, 역할, 현재 상태를 구조화하여 출력
 * 
 * @param player_id: 플레이어 ID (0 또는 1)
 * @param status: 현재 상태 문자열
 */
void print_player_status(int player_id, const char* status) {
    printf("╭─────────────────────────────────────────────────────────────╮\n");
    printf("│  👤 플레이어 정보                                             │\n");
    printf("├─────────────────────────────────────────────────────────────┤\n");
    printf("│  🆔 ID: %d                                                   │\n", player_id);
    printf("│  🎭 역할: %s                                                 │\n", player_id == 0 ? "선공" : "후공");
    printf("│  📊 상태: %s                                                 │\n", status);
    printf("╰─────────────────────────────────────────────────────────────╯\n");
    printf("\n");
}

/**
 * 게임 규칙 및 명령어 도움말 출력
 * 새로운 사용자도 쉽게 이해할 수 있도록 상세한 가이드 제공
 */
void print_game_rules() {
    printf("╭─────────────────────────────────────────────────────────────╮\n");
    printf("│  📋 게임 규칙 & 명령어                                         │\n");
    printf("├─────────────────────────────────────────────────────────────┤\n");
    printf("│                                                             │\n");
    printf("│  🎯 목표: 상대방의 3자리 숫자를 먼저 맞추면 승리!               │\n");
    printf("│                                                             │\n");
    printf("│  📊 결과 해석:                                               │\n");
    printf("│     ⚡ 스트라이크: 숫자와 위치가 모두 정확                     │\n");
    printf("│     🔮 볼: 숫자는 맞지만 위치가 틀림                          │\n");
    printf("│                                                             │\n");
    printf("│  💻 명령어:                                                  │\n");
    printf("│     🔹 set <3자리숫자>    - 내 숫자 설정 (예: set 123)       │\n");
    printf("│     🔹 guess <3자리숫자>  - 상대방 숫자 추측 (예: guess 456) │\n");
    printf("│     🔹 help              - 도움말 다시 보기                 │\n");
    printf("│     🔹 quit              - 게임 종료                       │\n");
    printf("│                                                             │\n");
    printf("╰─────────────────────────────────────────────────────────────╯\n");
    printf("\n");
}

/**
 * 상대방 대기 중 애니메이션 출력
 * 동적인 점 애니메이션으로 대기 상태를 시각화
 */
void print_waiting_animation() {
    printf("  ⏳ 상대방을 기다리는 중");
    for (int i = 0; i < 3; i++) {
        printf(".");
        fflush(stdout);
        usleep(200000); // 0.2초 대기
    }
    printf(" 🎭\n");
    printf("  💫 곧 상대방이 접속할 예정입니다! 조금만 기다려주세요~\n\n");
}

/**
 * 턴 상태 표시 (내 턴 vs 상대방 턴)
 * 현재 게임 진행 상황을 명확하게 사용자에게 알림
 * 
 * @param is_my_turn: 내 턴 여부 (1: 내턴, 0: 상대턴)
 */
void print_turn_indicator(int is_my_turn) {
    if (is_my_turn) {
        printf("╭─────────────────────────────────────────────────────────────╮\n");
        printf("│  🎯 당신의 턴입니다! YOUR TURN! 🎯                           │\n");
        printf("├─────────────────────────────────────────────────────────────┤\n");
        printf("│                                                             │\n");
        printf("│  🔥 상대방의 숫자를 추측해보세요!                            │\n");
        printf("│  💡 명령어: guess <3자리숫자>                               │\n");
        printf("│  📝 예시: guess 123, guess 456                             │\n");
        printf("│                                                             │\n");
        printf("╰─────────────────────────────────────────────────────────────╯\n");
    } else {
        printf("╭─────────────────────────────────────────────────────────────╮\n");
        printf("│  ⏰ 상대방의 턴 - 대기 중... WAITING... ⏰                   │\n");
        printf("├─────────────────────────────────────────────────────────────┤\n");
        printf("│                                                             │\n");
        printf("│  🤔 상대방이 추측하고 있습니다...                            │\n");
        printf("│  ☕ 커피 한 잔 하며 기다려보세요!                            │\n");
        printf("│                                                             │\n");
        printf("╰─────────────────────────────────────────────────────────────╯\n");
    }
    printf("\n");
}

/**
 * 추측 결과 시각화 (스트라이크/볼 표시)
 * 그래픽 요소를 사용하여 결과를 직관적으로 표현
 * 
 * @param guess: 추측한 숫자
 * @param strikes: 스트라이크 개수
 * @param balls: 볼 개수
 * @param attempts: 현재까지 시도 횟수
 */
void print_result_board(const char* guess, int strikes, int balls, int attempts) {
    printf("╭─────────────────────────────────────────────────────────────╮\n");
    printf("│  📊 추측 결과 - GUESS RESULT 📊                              │\n");
    printf("├─────────────────────────────────────────────────────────────┤\n");
    printf("│                                                             │\n");
    printf("│  🎯 추측한 숫자: %s                                          │\n", guess);
    printf("│                                                             │\n");
    
    // 스트라이크 시각화 (불꽃 이모지 사용)
    printf("│  ⚡ 스트라이크: %d  ", strikes);
    for (int i = 0; i < strikes; i++) printf("🔥");
    for (int i = strikes; i < 3; i++) printf("⚪");
    printf("                              │\n");
    
    // 볼 시각화 (다이아몬드 이모지 사용)
    printf("│  🔮 볼: %d         ", balls);
    for (int i = 0; i < balls; i++) printf("💎");
    for (int i = balls; i < 3; i++) printf("⚫");
    printf("                              │\n");
    
    printf("│                                                             │\n");
    printf("│  📈 시도 횟수: %d번                                          │\n", attempts);
    printf("│                                                             │\n");
    
    // 정답인 경우 축하 메시지
    if (strikes == 3) {
        printf("│  🎊🎊🎊 축하합니다! 정답입니다! 🎊🎊🎊                    │\n");
    }
    
    printf("╰─────────────────────────────────────────────────────────────╯\n");
    printf("\n");
}

void print_victory_screen() {
    printf("\n\n");
    printf("    🎊🎊🎊🎊🎊🎊🎊🎊🎊🎊🎊🎊🎊🎊🎊🎊🎊🎊🎊🎊\n");
    printf("   🎊                                                    🎊\n");
    printf("  🎊     🏆✨ VICTORY! 승리! CONGRATULATIONS! ✨🏆       🎊\n");
    printf(" 🎊                                                      🎊\n");
    printf("🎊        🎯 YOU ARE THE BASEBALL CHAMPION! 🎯           🎊\n");
    printf(" 🎊                                                      🎊\n");
    printf("  🎊     🌟 최고의 추리 실력을 보여주셨습니다! 🌟          🎊\n");
    printf("   🎊                                                    🎊\n");
    printf("    🎊🎊🎊🎊🎊🎊🎊🎊🎊🎊🎊🎊🎊🎊🎊🎊🎊🎊🎊🎊\n");
    printf("\n");
}

void print_defeat_screen() {
    printf("\n\n");
    printf("    😢😢😢😢😢😢😢😢😢😢😢😢😢😢😢😢😢😢😢😢\n");
    printf("   😢                                                    😢\n");
    printf("  😢     💪 아쉽지만 좋은 경기였습니다! 💪                  😢\n");
    printf(" 😢                                                      😢\n");
    printf("😢        🔥 다음번엔 더 잘할 수 있을 거예요! 🔥           😢\n");
    printf(" 😢                                                      😢\n");
    printf("  😢     ⭐ 포기하지 마세요! 재도전하세요! ⭐               😢\n");
    printf("   😢                                                    😢\n");
    printf("    😢😢😢😢😢😢😢😢😢😢😢😢😢😢😢😢😢😢😢😢\n");
    printf("\n");
}

void print_game_over_info(const char* my_number, const char* opponent_number) {
    printf("╭─────────────────────────────────────────────────────────────╮\n");
    printf("│  📝 게임 결과 - FINAL RESULT 📝                              │\n");
    printf("├─────────────────────────────────────────────────────────────┤\n");
    printf("│                                                             │\n");
    printf("│  🔐 당신의 숫자:   %s                                       │\n", my_number);
    printf("│  🎭 상대방 숫자:   %s                                       │\n", opponent_number);
    printf("│                                                             │\n");
    printf("│  💡 잘 기억해두세요! 다음 게임에 도움이 될 거예요!            │\n");
    printf("│                                                             │\n");
    printf("╰─────────────────────────────────────────────────────────────╯\n");
    printf("\n");
}

void print_input_prompt() {
    printf("┌─ 💬 명령어 입력 ─────────────────────────────────────────────┐\n");
    printf("│  ");
    fflush(stdout);
}

void print_success_message(const char* message) {
    printf("┌─ ✅ 성공 ──────────────────────────────────────────────────┐\n");
    printf("│  %s\n", message);
    printf("└─────────────────────────────────────────────────────────────┘\n");
    printf("\n");
}

void print_error_message(const char* message) {
    printf("┌─ ❌ 오류 ──────────────────────────────────────────────────┐\n");
    printf("│  %s\n", message);
    printf("└─────────────────────────────────────────────────────────────┘\n");
    printf("\n");
}

// ──────────────────────────────────────────────────────────
// 명령어 처리
// ──────────────────────────────────────────────────────────
int handle_user_input(int sockfd) {
    char input[256];
    print_input_prompt();
    
    if (!fgets(input, sizeof(input), stdin)) {
        return -1; // EOF
    }
    
    printf("└─────────────────────────────────────────────────────────────┘\n\n");
    
    // 줄바꿈 제거
    input[strcspn(input, "\n")] = '\0';
    
    // 빈 입력
    if (strlen(input) == 0) {
        return 0;
    }
    
    // quit 명령
    if (strcmp(input, "quit") == 0) {
        printf("🚪 게임을 종료합니다... 안녕히 가세요! 👋\n");
        return -1;
    }
    
    // help 명령
    if (strcmp(input, "help") == 0) {
        print_game_rules();
        return 0;
    }
    
    // set 명령 (숫자 설정)
    if (strncmp(input, "set ", 4) == 0) {
        if (number_set) {
            print_error_message("이미 숫자를 설정했습니다!");
            return 0;
        }
        
        char *number = input + 4;
        
        if (!is_valid_number(number)) {
            print_error_message("올바르지 않은 숫자입니다! 3자리 서로 다른 숫자를 입력하세요.");
            printf("   💡 예시: set 123, set 789\n\n");
            return 0;
        }
        
        struct json_object *jmsg = create_message(ACTION_SET_NUMBER);
        json_object_object_add(jmsg, "number", json_object_new_string(number));
        send_json(sockfd, jmsg);
        json_object_put(jmsg);
        
        char success_msg[100];
        snprintf(success_msg, sizeof(success_msg), "숫자를 설정했습니다: %s ✨", number);
        print_success_message(success_msg);
        return 0;
    }
    
    // guess 명령 (추측)
    if (strncmp(input, "guess ", 6) == 0) {
        if (!my_turn) {
            print_error_message("지금은 당신의 턴이 아닙니다!");
            return 0;
        }
        
        char *guess = input + 6;
        
        if (!is_valid_number(guess)) {
            print_error_message("올바르지 않은 숫자입니다! 3자리 서로 다른 숫자를 입력하세요.");
            printf("   💡 예시: guess 123, guess 789\n\n");
            return 0;
        }
        
        struct json_object *jmsg = create_message(ACTION_GUESS);
        json_object_object_add(jmsg, "guess", json_object_new_string(guess));
        send_json(sockfd, jmsg);
        json_object_put(jmsg);
        
        return 0;
    }
    
    // 알 수 없는 명령
    print_error_message("알 수 없는 명령어입니다. 'help'를 입력하여 도움말을 확인하세요.");
    return 0;
}

// ──────────────────────────────────────────────────────────
// 서버 메시지 처리
// ──────────────────────────────────────────────────────────
int handle_server_message(int sockfd) {
    struct json_object *jmsg = recv_json(sockfd);
    if (!jmsg) {
        print_error_message("서버와의 연결이 끊어졌습니다.");
        return -1;
    }
    
    struct json_object *jact = NULL;
    if (!json_object_object_get_ex(jmsg, "action", &jact)) {
        json_object_put(jmsg);
        return 0;
    }
    
    const char *action = json_object_get_string(jact);
    
    // 플레이어 ID 할당
    if (strcmp(action, ACTION_ASSIGN_ID) == 0) {
        struct json_object *jpid = NULL;
        if (json_object_object_get_ex(jmsg, "player_id", &jpid)) {
            my_player_id = json_object_get_int(jpid);
            print_player_status(my_player_id, "연결됨 ✅");
        }
    }
    
    // 대기 메시지
    else if (strcmp(action, ACTION_WAIT_PLAYER) == 0) {
        print_waiting_animation();
    }
    
    // 게임 시작
    else if (strcmp(action, ACTION_GAME_START) == 0) {
        game_started = 1;
        clear_screen();
        print_game_header();
        print_game_rules();
        
        printf("🎮 게임이 시작되었습니다! 이제 당신의 비밀 숫자를 설정하세요!\n");
        printf("💡 'set <3자리숫자>' 명령으로 숫자를 설정하세요! (예: set 123)\n\n");
    }
    
    // 숫자 설정 완료
    else if (strcmp(action, ACTION_NUMBER_SET) == 0) {
        number_set = 1;
        print_success_message("숫자가 성공적으로 설정되었습니다! 상대방을 기다리는 중...");
    }
    
    // 내 턴
    else if (strcmp(action, ACTION_YOUR_TURN) == 0) {
        my_turn = 1;
        print_turn_indicator(1);
    }
    
    // 상대방 턴
    else if (strcmp(action, ACTION_WAIT_TURN) == 0) {
        my_turn = 0;
        print_turn_indicator(0);
    }
    
    // 추측 결과
    else if (strcmp(action, ACTION_GUESS_RESULT) == 0) {
        struct json_object *jguess = NULL, *jstrikes = NULL, *jballs = NULL, *jattempts = NULL;
        
        if (json_object_object_get_ex(jmsg, "guess", &jguess) &&
            json_object_object_get_ex(jmsg, "strikes", &jstrikes) &&
            json_object_object_get_ex(jmsg, "balls", &jballs) &&
            json_object_object_get_ex(jmsg, "attempts", &jattempts)) {
            
            const char *guess = json_object_get_string(jguess);
            int strikes = json_object_get_int(jstrikes);
            int balls = json_object_get_int(jballs);
            int attempts = json_object_get_int(jattempts);
            
            print_result_board(guess, strikes, balls, attempts);
        }
    }
    
    // 게임 종료
    else if (strcmp(action, ACTION_GAME_OVER) == 0) {
        struct json_object *jresult = NULL;
        struct json_object *jyour_num = NULL, *jopp_num = NULL;
        
        if (json_object_object_get_ex(jmsg, "result", &jresult)) {
            const char *result = json_object_get_string(jresult);
            if (strcmp(result, "victory") == 0) {
                print_victory_screen();
            } else {
                print_defeat_screen();
            }
        }
        
        // 정답 공개
        if (json_object_object_get_ex(jmsg, "your_number", &jyour_num) &&
            json_object_object_get_ex(jmsg, "opponent_number", &jopp_num)) {
            print_game_over_info(json_object_get_string(jyour_num), 
                                json_object_get_string(jopp_num));
        }
        
        printf("🚪 게임이 종료됩니다... 수고하셨습니다! 👏\n\n");
        json_object_put(jmsg);
        return -1;
    }
    
    // 에러 메시지
    else if (strcmp(action, ACTION_ERROR) == 0) {
        struct json_object *jmessage = NULL;
        if (json_object_object_get_ex(jmsg, "message", &jmessage)) {
            print_error_message(json_object_get_string(jmessage));
        }
    }
    
    json_object_put(jmsg);
    return 0;
}

// ──────────────────────────────────────────────────────────
// 메인 함수
// ──────────────────────────────────────────────────────────
int main(int argc, char *argv[]) {
    if (argc != 3) {
        printf("사용법: %s <서버IP> <포트>\n", argv[0]);
        return 1;
    }
    
    const char *server_ip = argv[1];
    int port = atoi(argv[2]);
    
    // 웰컴 스크린 표시
    print_welcome_screen();
    
    // 소켓 생성
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("socket");
        return 1;
    }
    
    // 서버 연결
    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port);
    inet_pton(AF_INET, server_ip, &serv_addr.sin_addr);
    
    if (connect(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        print_error_message("서버에 연결할 수 없습니다. 서버가 실행 중인지 확인해주세요.");
        return 1;
    }
    
    clear_screen();
    print_game_header();
    printf("🎊 서버에 성공적으로 연결되었습니다! 🎊\n\n");
    print_game_rules();
    
    // 메인 루프 (select 사용)
    fd_set read_fds;
    int max_fd = sockfd;
    
    while (1) {
        FD_ZERO(&read_fds);
        FD_SET(STDIN_FILENO, &read_fds);  // 표준 입력
        FD_SET(sockfd, &read_fds);        // 서버 소켓
        
        int activity = select(max_fd + 1, &read_fds, NULL, NULL, NULL);
        if (activity < 0) {
            perror("select");
            break;
        }
        
        // 서버 메시지 처리
        if (FD_ISSET(sockfd, &read_fds)) {
            if (handle_server_message(sockfd) < 0) {
                break;
            }
        }
        
        // 사용자 입력 처리
        if (FD_ISSET(STDIN_FILENO, &read_fds)) {
            if (handle_user_input(sockfd) < 0) {
                break;
            }
        }
    }
    
    close(sockfd);
    return 0;
} 