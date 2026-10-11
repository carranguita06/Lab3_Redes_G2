#!/bin/bash
# =====================================================
# Escenario del laboratorio - Versión TCP
# 1 broker + 4 suscriptores + 2 publicadores
# Captura automática con tshark
# =====================================================

PORT=8080
LOGDIR="logs_tcp"
PCAP_FILE="tcp_pubsub.pcap"
mkdir -p $LOGDIR

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
    # Detener captura PRIMERO (para que escriba el archivo correctamente)
    kill $TSHARK_PID 2>/dev/null
    sleep 2          # margen para flush del .pcap
    
    # Luego matar procesos de la aplicación
    kill $BROKER_PID $SUB1_PID $SUB2_PID $SUB3_PID $SUB4_PID \
         $PUB1_PID $PUB2_PID 2>/dev/null
    wait 2>/dev/null
}
trap cleanup EXIT

echo "======================================================"
echo " ESCENARIO TCP: 1 broker, 4 suscriptores, 2 publicadores"
echo "======================================================"

# ---------- 0) INICIAR CAPTURA TSHARK ----------
# ★ Captura en loopback (lo) con filtro TCP para el puerto 8080
rm -f $PCAP_FILE
tshark -i lo -f "tcp port 8080" -w $PCAP_FILE > /dev/null 2>&1 &
TSHARK_PID=$!
sleep 1   # dar tiempo a que tshark empiece a capturar

# Verificar que tshark está corriendo
if ! ps -p $TSHARK_PID > /dev/null 2>&1; then
    echo "[ERROR] tshark no pudo iniciarse"
    echo "Asegúrate de estar en WSL y haber instalado wireshark-cli"
    exit 1
fi
echo "-> Captura iniciada (pcap: $PCAP_FILE)"

# ---------- 1) BROKER ----------
./broker_tcp > $LOGDIR/broker.log 2>&1 &
BROKER_PID=$!
sleep 1

# ---------- 2) CUATRO SUSCRIPTORES ----------
#   Sub1: solo PartidoA
#   Sub2: solo PartidoB
#   Sub3: multi-tema (PartidoA,PartidoB)  ← ★ AHORA FUNCIONA EN TCP (corrección)
#   Sub4: solo PartidoA
./subscriber_tcp PartidoA > $LOGDIR/sub1.log 2>&1 &
SUB1_PID=$!
./subscriber_tcp PartidoB > $LOGDIR/sub2.log 2>&1 &
SUB2_PID=$!
./subscriber_tcp PartidoA,PartidoB > $LOGDIR/sub3.log 2>&1 &
SUB3_PID=$!
./subscriber_tcp PartidoA > $LOGDIR/sub4.log 2>&1 &
SUB4_PID=$!

# Dar tiempo a que los 4 registros lleguen al broker
sleep 2

echo "-> 4 suscriptores activos:"
ps -p $SUB1_PID,$SUB2_PID,$SUB3_PID,$SUB4_PID -o pid,cmd --no-headers

# ---------- 3) DOS PUBLICADORES ----------
# Cada publisher envía 10 mensajes con sleep(2) => ~20 s
./publisher_tcp PartidoA > $LOGDIR/pub1.log 2>&1 &
PUB1_PID=$!
./publisher_tcp PartidoB > $LOGDIR/pub2.log 2>&1 &
PUB2_PID=$!

echo "-> Publicadores lanzados, esperando que terminen (~22 s)..."

# Esperar a que AMBOS publicadores completen sus 10 mensajes
wait $PUB1_PID $PUB2_PID

# Margen para que llegue el último mensaje reenviado
sleep 2

# ---------- 4) VERIFICACIÓN ----------
echo ""
echo "======================================================"
echo " RESULTADOS"
echo "======================================================"

# Distribución esperada:
#   PartidoA publica 10 mensajes  -> Sub1, Sub3, Sub4 los reciben
#   PartidoB publica 10 mensajes  -> Sub2, Sub3 los reciben
RCV1=$(grep -c "NOTICIA RECIBIDA" $LOGDIR/sub1.log)   # esperado: 10
RCV2=$(grep -c "NOTICIA RECIBIDA" $LOGDIR/sub2.log)   # esperado: 10
RCV3=$(grep -c "NOTICIA RECIBIDA" $LOGDIR/sub3.log)   # esperado: 20 (multi-tema)
RCV4=$(grep -c "NOTICIA RECIBIDA" $LOGDIR/sub4.log)   # esperado: 10

check 10 $RCV1 "Sub1 (PartidoA)"
check 10 $RCV2 "Sub2 (PartidoB)"
check 20 $RCV3 "Sub3 (PartidoA+PartidoB)"  ← ★ NUEVO: ahora sí funciona en TCP
check 10 $RCV4 "Sub4 (PartidoA)"

# Que el broker haya reenviado cada mensaje al número correcto de suscriptores
REENVIO1=$(grep "entregado a 3 suscriptor" $LOGDIR/broker.log | wc -l)
REENVIO2=$(grep "entregado a 2 suscriptor" $LOGDIR/broker.log | wc -l)
check 10 $REENVIO1 "Broker reenvía PartidoA a 3 suscriptores"
check 10 $REENVIO2 "Broker reenvía PartidoB a 2 suscriptores"

# Cada publicador envió efectivamente 10 mensajes
SENT1=$(grep -c "Evento.*enviado" $LOGDIR/pub1.log)
SENT2=$(grep -c "Evento.*enviado" $LOGDIR/pub2.log)
check 10 $SENT1 "Publisher 1 envió 10 mensajes"
check 10 $SENT2 "Publisher 2 envió 10 mensajes"

# ★ ★ ★ CONTRASTE CLAVE TCP vs UDP ★ ★ ★
# En TCP, cuando un cliente se desconecta, el otro EXTREMOS LO SABE.
# Esto es lo que preguntaba el laboratorio sobre recuperación de sesión.
echo ""
echo "--- Contraste TCP/UDP (para el informe) ---"
if grep -q "cerró la conexión" $LOGDIR/broker.log; then
    echo "[INFO] El broker DETECTÓ desconexiones limpias (segmentos FIN)"
    echo "[INFO] En UDP esto no ocurriría: no hay detección de caída"
else
    echo "[INFO] Todas las conexiones terminaron normalmente"
fi

echo ""
echo "======================================================"
echo " TOTAL: $pass PASS / $fail FAIL"
echo "======================================================"
echo "Archivos generados:"
echo "  - Log:     ./$LOGDIR/*.log"
echo "  - Captura: ./$(basename $PCAP_FILE)"

[ $fail -eq 0 ]