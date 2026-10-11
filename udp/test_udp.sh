#!/bin/bash
# =====================================================
# Escenario del laboratorio: 1 broker + 4 suscriptores + 2 publicadores
# Uso: chmod +x test_udp.sh && ./test_udp.sh
# =====================================================

PORT=9090
LOGDIR="logs_udp"
mkdir -p $LOGDIR

# ---------- 0) INICIAR CAPTURA ----------
PCAP="udp_pubsub.pcap"
rm -f $PCAP
tshark -i lo -f "udp port 9090" -w $PCAP > /dev/null 2>&1 &
TSHARK_PID=$!
sleep 1

pass=0
fail=0

check() {
    # $1 = valor esperado, $2 = valor obtenido, $3 = descripción
    if [ "$1" -eq "$2" ]; then
        echo "[PASS] $3 (esperados: $1, recibidos: $2)"
        pass=$((pass+1))
    else
        echo "[FAIL] $3 (esperados: $1, recibidos: $2)"
        fail=$((fail+1))
    fi
}

cleanup() {
    kill $TSHARK_PID 2>/dev/null    # detener la captura PRIMERO
    sleep 2                          # margen para que tshark flushee el pcap
    kill $BROKER_PID $SUB1_PID $SUB2_PID $SUB3_PID $SUB4_PID \
         $PUB1_PID $PUB2_PID 2>/dev/null
    wait 2>/dev/null
}
trap cleanup EXIT

echo "======================================================"
echo " ESCENARIO UDP: 1 broker, 4 suscriptores, 2 publicadores"
echo "======================================================"

# ---------- 1) BROKER ----------
./broker_udp > $LOGDIR/broker.log 2>&1 &
BROKER_PID=$!
sleep 1

# ---------- 2) CUATRO SUSCRIPTORES ----------
#   Sub1: solo partido1
#   Sub2: solo partido2
#   Sub3: multi-tema (partido1,partido2)
#   Sub4: solo partido1
./subscriber_udp partido1 > $LOGDIR/sub1.log 2>&1 &
SUB1_PID=$!
./subscriber_udp partido2 > $LOGDIR/sub2.log 2>&1 &
SUB2_PID=$!
./subscriber_udp partido1,partido2 > $LOGDIR/sub3.log 2>&1 &
SUB3_PID=$!
./subscriber_udp partido1 > $LOGDIR/sub4.log 2>&1 &
SUB4_PID=$!

# Dar tiempo a que los 4 registros lleguen al broker
sleep 2

echo "-> 4 suscriptores activos:"
ps -p $SUB1_PID,$SUB2_PID,$SUB3_PID,$SUB4_PID -o pid,cmd --no-headers

# ---------- 3) DOS PUBLICADORES ----------
# Cada publisher envía 10 mensajes con sleep(2) => ~20 s
./publisher_udp partido1 > $LOGDIR/pub1.log 2>&1 &
PUB1_PID=$!
./publisher_udp partido2 > $LOGDIR/pub2.log 2>&1 &
PUB2_PID=$!

echo "-> Publicadores lanzados, esperando que terminen (~22 s)..."

# Esperar a que AMBOS publicadores completen sus 10 mensajes
wait $PUB1_PID $PUB2_PID

# Margen para que llegue el último datagrama reenviado
sleep 2

# ---------- 4) VERIFICACIÓN ----------
echo ""
echo "======================================================"
echo " RESULTADOS"
echo "======================================================"

# Distribución esperada:
#   partido1 publica 10 mensajes  -> Sub1, Sub3, Sub4 los reciben
#   partido2 publica 10 mensajes  -> Sub2, Sub3 los reciben
RCV1=$(grep -c "Recibido" $LOGDIR/sub1.log)   # esperado: 10
RCV2=$(grep -c "Recibido" $LOGDIR/sub2.log)   # esperado: 10
RCV3=$(grep -c "Recibido" $LOGDIR/sub3.log)   # esperado: 20 (multi-tema)
RCV4=$(grep -c "Recibido" $LOGDIR/sub4.log)   # esperado: 10

check 10 $RCV1 "Sub1 (partido1)"
check 10 $RCV2 "Sub2 (partido2)"
check 20 $RCV3 "Sub3 (partido1+partido2)"
check 10 $RCV4 "Sub4 (partido1)"

# Que el broker haya reenviado cada mensaje al número correcto de suscriptores
REENVIO1=$(grep "Mensaje \[partido1\]" $LOGDIR/broker.log | grep -c "3 suscriptor")
REENVIO2=$(grep "Mensaje \[partido2\]" $LOGDIR/broker.log | grep -c "2 suscriptor")
check 10 $REENVIO1 "Broker reenvía partido1 a 3 suscriptores"
check 10 $REENVIO2 "Broker reenvía partido2 a 2 suscriptores"

# Cada publicador envió efectivamente 10 mensajes
SENT1=$(grep -c "Enviado" $LOGDIR/pub1.log)
SENT2=$(grep -c "Enviado" $LOGDIR/pub2.log)
check 10 $SENT1 "Publisher 1 envió 10 mensajes"
check 10 $SENT2 "Publisher 2 envió 10 mensajes"

echo ""
echo "======================================================"
echo " TOTAL: $pass PASS / $fail FAIL"
echo "======================================================"
echo "Logs en ./$LOGDIR/"

[ $fail -eq 0 ]