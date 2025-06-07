#!/bin/bash

# 숫자 야구 네트워크 게임 - 빠른 시연 스크립트
# 자동화된 데모 (발표 연습용)

echo "╔═══════════════════════════════════════════════════════════════╗"
echo "║              🚀 빠른 시연 모드 - 자동 실행 🚀                   ║"
echo "║                                                               ║"
echo "║  📡 서버와 2개 클라이언트를 순차적으로 실행합니다                 ║"
echo "║  ⏱️  총 시연 시간: 약 30초                                     ║"
echo "║                                                               ║"
echo "╚═══════════════════════════════════════════════════════════════╝"
echo ""

# 컴파일 확인
if [ ! -f "./baseball_server" ] || [ ! -f "./baseball_client" ]; then
    echo "🔨 프로그램을 먼저 컴파일합니다..."
    make clean && make all
    echo "✅ 컴파일 완료!"
    echo ""
fi

echo "🎬 [1/3] 서버 시작..."
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
./baseball_server 8080 &
SERVER_PID=$!
sleep 2

echo ""
echo "🎮 [2/3] 첫 번째 클라이언트 접속..."
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo -e "set 123\nquit" | ./baseball_client 127.0.0.1 8080 &
CLIENT1_PID=$!
sleep 3

echo ""
echo "🎮 [3/3] 두 번째 클라이언트 접속..."
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo -e "set 456\nquit" | ./baseball_client 127.0.0.1 8080 &
CLIENT2_PID=$!
sleep 5

echo ""
echo "🧹 정리 중..."
kill $SERVER_PID $CLIENT1_PID $CLIENT2_PID 2>/dev/null
wait $SERVER_PID $CLIENT1_PID $CLIENT2_PID 2>/dev/null

echo ""
echo "✅ 빠른 시연 완료!"
echo "🎯 실제 발표에서는 demo_server.sh, demo_client1.sh, demo_client2.sh를 사용하세요!" 