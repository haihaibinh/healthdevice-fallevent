const mqtt = require('mqtt');
const { WebSocketServer } = require('ws');
const pool = require('./db');

// ─── MQTT BROKER CONFIG ──────────────────────────────────────────────────────
const HIVEMQ_HOST = process.env.HIVEMQ_HOST || 'broker.hivemq.com';
const HIVEMQ_PORT = parseInt(process.env.HIVEMQ_PORT, 10) || 1883;
const MQTT_USER   = process.env.MQTT_USER   || '';
const MQTT_PASS   = process.env.MQTT_PASS   || '';
const MQTT_TOPIC  = process.env.MQTT_TOPIC  || 'health/device/node2';

// ─── WEBSOCKET CONFIG ────────────────────────────────────────────────────────
const WS_PORT = parseInt(process.env.WS_PORT, 10) || 3001;

const wsClients = new Set();
let started = false;

// ─── HELPERS ─────────────────────────────────────────────────────────────────
function normalizeTimestamp(value) {
  const numericValue = Number(value);
  const currentUnixTime = Math.floor(Date.now() / 1000);

  if (!Number.isFinite(numericValue)) return currentUnixTime;
  if (numericValue < 1700000000)      return currentUnixTime;
  if (numericValue > currentUnixTime + 86400) return currentUnixTime;

  return Math.floor(numericValue);
}

// ─── MAIN ─────────────────────────────────────────────────────────────────────
function startMqttSubscriber() {
  if (started) return;
  started = true;

  // 1. WebSocket server — để push data real-time ra frontend
  const wss = new WebSocketServer({ port: WS_PORT });
  wss.on('connection', (ws) => {
    wsClients.add(ws);
    ws.on('close', () => wsClients.delete(ws));
    ws.on('error', () => wsClients.delete(ws));
  });
  console.log(`[WS] WebSocket server listening on port ${WS_PORT}`);

  // 2. Kết nối tới MQTT Broker
  const isTls = HIVEMQ_PORT === 8883 || HIVEMQ_PORT === 8884 || HIVEMQ_PORT === 443;
  const connectionOptions = {
    host:     HIVEMQ_HOST,
    port:     HIVEMQ_PORT,
    protocol: isTls ? 'mqtts' : 'mqtt',
  };

  if (MQTT_USER) connectionOptions.username = MQTT_USER;
  if (MQTT_PASS) connectionOptions.password = MQTT_PASS;
  if (isTls) connectionOptions.rejectUnauthorized = true;

  const client = mqtt.connect(connectionOptions);

  // 3. Subscribe khi kết nối thành công
  client.on('connect', () => {
    console.log(`[MQTT] Connected to HiveMQ Cloud (${HIVEMQ_HOST}:${HIVEMQ_PORT})`);
    client.subscribe(MQTT_TOPIC, { qos: 1 }, (err) => {
      if (err) {
        console.error('[MQTT] Subscribe error:', err.message);
      } else {
        console.log(`[MQTT] Subscribed to topic: ${MQTT_TOPIC}`);
      }
    });
  });
  client.on('message', (topic, message) => {
  console.log('[MQTT] RAW MESSAGE RECEIVED:', topic, message.toString());
});

client.on('packetsend', (packet) => {
  console.log('[MQTT] Packet sent:', packet.cmd);
});

client.on('packetreceive', (packet) => {
  console.log('[MQTT] Packet received:', packet.cmd);
});

  // 4. Xử lý message nhận được
  client.on('message', async (topic, message) => {
      console.log('======================');
  console.log('[TOPIC]', topic);
  console.log('[RAW]', message.toString());
  console.log('======================');
    try {
      const data = JSON.parse(message.toString());
      const normalizedTimestamp = normalizeTimestamp(data.timestamp);

      // Lưu vào PostgreSQL
      await pool.query(
        `INSERT INTO sensor_data
          (device_id, timestamp, seq, mpu_status, battery_pct, voltage,
           prediction, event, acc_mag, angle, ax_g, ay_g, az_g)
         VALUES ($1, $2, $3, $4, $5, $6, $7, $8, $9, $10, $11, $12, $13)`,
        [
          data.device_id,
          normalizedTimestamp,
          data.seq ?? null,
          data.mpu_status != null ? (data.mpu_status === 1 || data.mpu_status === true || data.mpu_status === 'true') : null,
          data.battery_pct ?? null,
          data.voltage != null ? parseFloat(data.voltage) : null,
          data.prediction ?? null,
          data.event ?? null,
          data.physics?.acc_mag ?? null,
          data.physics?.angle ?? null,
          data.physics?.ax_g ?? null,
          data.physics?.ay_g ?? null,
          data.physics?.az_g ?? null,
        ],
      );

      console.log(`[MQTT] Saved to DB | device: ${data.device_id} | ts: ${normalizedTimestamp}`);

      // Broadcast ra tất cả WebSocket clients
      const payload = JSON.stringify({
        topic,
        data: { ...data, timestamp: normalizedTimestamp },
      });

      for (const ws of wsClients) {
        if (ws.readyState === ws.OPEN) {
          ws.send(payload);
        }
      }
    } catch (err) {
      console.error('[MQTT] Error processing message:', err.message);
    }
  });

  // 5. Xử lý lỗi & reconnect
  client.on('error', (err) => {
    console.error('[MQTT] Connection error:', err.message);
  });

  client.on('reconnect', () => {
    console.log('[MQTT] Reconnecting to HiveMQ Cloud...');
  });

  client.on('offline', () => {
    console.warn('[MQTT] Client went offline');
  });
}

module.exports = { startMqttSubscriber };
