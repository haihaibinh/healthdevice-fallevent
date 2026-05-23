const mqtt = require('mqtt');
const { WebSocketServer } = require('ws');
const pool = require('./db');

// ─── HIVEMQ CLOUD CONFIG ────────────────────────────────────────────────────
const HIVEMQ_HOST = process.env.HIVEMQ_HOST || '3c42211a3c8f4decbdf20c41e2b72fcf.s1.eu.hivemq.cloud';
const HIVEMQ_PORT = parseInt(process.env.HIVEMQ_PORT, 10) || 8883;
const MQTT_USER   = process.env.MQTT_USER   || 'esp32_health_device';
const MQTT_PASS   = process.env.MQTT_PASS   || 'Sa123456';
const MQTT_TOPIC  = process.env.MQTT_TOPIC  || 'health/#';

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

  // 2. Kết nối thẳng lên HiveMQ Cloud qua TLS (mqtts://)
  const client = mqtt.connect({
    host:     HIVEMQ_HOST,
    port:     HIVEMQ_PORT,
    protocol: 'mqtts',       // TLS — bắt buộc với HiveMQ Cloud
    username: MQTT_USER,
    password: MQTT_PASS,
    // HiveMQ Cloud dùng cert công khai — Node.js tự verify, không cần CA file riêng
    rejectUnauthorized: true,
  });

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
          (device_id, timestamp, battery_node1, battery_node2,
           acceleration, emg, angle,
           heart_rate, spo2, hrv,
           risk_score, event)
         VALUES ($1,$2,$3,$4,$5,$6,$7,$8,$9,$10,$11,$12)`,
[
          data.device_id,
          normalizedTimestamp,
          data.battery_node1 ?? null,
          data.battery_node2 ?? null,
          data.physics?.acceleration ?? data.raw_data?.acc_g ?? null,
          data.physics?.emg ?? data.raw_data?.emg_rms ?? null,
          data.physics?.angle ?? data.raw_data?.angle_deg ?? null,
          data.biometric?.heart_rate ?? null,
          data.biometric?.spo2 ?? null,
          data.biometric?.hrv ?? null,
          data.risk_score ?? data.inference?.muscle_status ?? null,
          data.event ?? data.inference?.fall_detected ?? null,
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
