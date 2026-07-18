const mqtt = require('mqtt');
const { WebSocketServer } = require('ws');
const pool = require('./db');

// ─── MQTT BROKER CONFIG ──────────────────────────────────────────────────────
const HIVEMQ_HOST = process.env.HIVEMQ_HOST || 'broker.hivemq.com';
const HIVEMQ_PORT = parseInt(process.env.HIVEMQ_PORT, 10) || 1883;
const MQTT_USER   = process.env.MQTT_USER   || '';
const MQTT_PASS   = process.env.MQTT_PASS   || '';
const MQTT_TOPICS = (process.env.MQTT_TOPICS || '').split(',').map((item) => item.trim()).filter(Boolean);
const MQTT_TOPIC   = process.env.MQTT_TOPIC || 'health/device/#';

// ─── WEBSOCKET CONFIG ────────────────────────────────────────────────────────
const WS_PORT = parseInt(process.env.WS_PORT, 10) || 3001;

const wsClients = new Set();
let started = false;

// ─── HELPERS ─────────────────────────────────────────────────────────────────
function normalizeTimestampSeconds(value) {
  const numericValue = Number(value);
  const currentUnixTime = Math.floor(Date.now() / 1000);

  if (!Number.isFinite(numericValue)) return currentUnixTime;
  if (numericValue > 1000000000000) return Math.floor(numericValue / 1000);
  if (numericValue < 1700000000) return currentUnixTime;
  if (numericValue > currentUnixTime + 86400) return currentUnixTime;

  return Math.floor(numericValue);
}

function normalizeTimestampMs(payload) {
  if (payload.timestamp_ms != null) {
    const numericMs = Number(payload.timestamp_ms);
    if (Number.isFinite(numericMs) && numericMs > 0) return Math.floor(numericMs);
  }

  if (payload.timestamp != null) {
    const numericTs = Number(payload.timestamp);
    if (Number.isFinite(numericTs) && numericTs > 1000000000000) return Math.floor(numericTs);
    if (Number.isFinite(numericTs) && numericTs > 0) return Math.floor(numericTs * 1000);
  }

  return 0;
}

function isEmgPayload(data) {
  return Array.isArray(data.emg_raw_list) || Array.isArray(data.emg_rms_list);
}

function normalizeTopics() {
  if (MQTT_TOPICS.length > 0) return [...new Set(MQTT_TOPICS)];
  return ['health/device/#'];
}

async function ensureSchema() {
  await pool.query(`
    ALTER TABLE sensor_data
      ADD COLUMN IF NOT EXISTS timestamp_ms BIGINT,
      ADD COLUMN IF NOT EXISTS clock_synced BOOLEAN,
      ADD COLUMN IF NOT EXISTS delayed_upload BOOLEAN;
  `);

  await pool.query(`
    CREATE TABLE IF NOT EXISTS emg_data (
      id SERIAL PRIMARY KEY,
      device_id VARCHAR(50) NOT NULL,
      timestamp BIGINT NOT NULL,
      timestamp_ms BIGINT,
      seq INT,
      emg_status BOOLEAN,
      emg_raw_list JSONB,
      emg_rms_list JSONB
    );
  `);

  await pool.query(`
    CREATE INDEX IF NOT EXISTS idx_emg_device_time
    ON emg_data(device_id, timestamp DESC);
  `);
}

async function saveEmgPayload(data, normalizedTimestamp, normalizedTimestampMs) {
  await pool.query(
    `INSERT INTO emg_data
      (device_id, timestamp, timestamp_ms, seq, emg_status, emg_raw_list, emg_rms_list)
     VALUES ($1, $2, $3, $4, $5, $6::jsonb, $7::jsonb)`,
    [
      data.device_id,
      normalizedTimestamp,
      normalizedTimestampMs || null,
      data.seq ?? null,
      data.emg_status != null ? (data.emg_status === 1 || data.emg_status === true || data.emg_status === 'true') : null,
      JSON.stringify(Array.isArray(data.emg_raw_list) ? data.emg_raw_list : []),
      JSON.stringify(Array.isArray(data.emg_rms_list) ? data.emg_rms_list : []),
    ],
  );
}

async function saveMotionPayload(data, normalizedTimestamp, normalizedTimestampMs) {
  await pool.query(
    `INSERT INTO sensor_data
      (device_id, timestamp, timestamp_ms, seq, mpu_status, battery_pct, voltage,
       prediction, event, clock_synced, delayed_upload,
       acc_mag, angle, ax_g, ay_g, az_g)
     VALUES ($1, $2, $3, $4, $5, $6, $7,
             $8, $9, $10, $11,
             $12, $13, $14, $15, $16)`,
    [
      data.device_id,
      normalizedTimestamp,
      normalizedTimestampMs || null,
      data.seq ?? null,
      data.mpu_status != null ? (data.mpu_status === 1 || data.mpu_status === true || data.mpu_status === 'true') : null,
      data.battery_pct ?? null,
      data.voltage != null ? parseFloat(data.voltage) : null,
      data.prediction ?? null,
      data.event ?? null,
      data.clock_synced != null ? (data.clock_synced === 1 || data.clock_synced === true || data.clock_synced === 'true') : null,
      data.delayed_upload != null ? (data.delayed_upload === 1 || data.delayed_upload === true || data.delayed_upload === 'true') : null,
      data.physics?.acc_mag ?? null,
      data.physics?.angle ?? null,
      data.physics?.ax_g ?? null,
      data.physics?.ay_g ?? null,
      data.physics?.az_g ?? null,
    ],
  );
}

function broadcastRealtime(topic, data, messageType) {
  const payload = JSON.stringify({
    topic,
    message_type: messageType,
    data,
  });

  for (const ws of wsClients) {
    if (ws.readyState === ws.OPEN) {
      ws.send(payload);
    }
  }
}

// ─── MAIN ─────────────────────────────────────────────────────────────────────
async function startMqttSubscriber() {
  if (started) return;
  started = true;

  await ensureSchema();

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
  const topicsToSubscribe = normalizeTopics();

  // 3. Subscribe khi kết nối thành công
  client.on('connect', () => {
    console.log(`[MQTT] Connected to HiveMQ Cloud (${HIVEMQ_HOST}:${HIVEMQ_PORT})`);
    client.subscribe(topicsToSubscribe, { qos: 1 }, (err) => {
      if (err) {
        console.error('[MQTT] Subscribe error:', err.message);
      } else {
        console.log(`[MQTT] Subscribed to topics: ${topicsToSubscribe.join(', ')}`);
      }
    });
  });

  // 4. Xử lý message nhận được
  client.on('message', async (topic, message) => {
    try {
      const data = JSON.parse(message.toString());

      const normalizedTimestamp = normalizeTimestampSeconds(data.timestamp_ms ?? data.timestamp);
      const normalizedTimestampMs = normalizeTimestampMs(data) || (normalizedTimestamp * 1000);

      if (isEmgPayload(data)) {
        await saveEmgPayload(data, normalizedTimestamp, normalizedTimestampMs);
        console.log(`[MQTT] Saved EMG | device: ${data.device_id} | ts: ${normalizedTimestamp}`);
        broadcastRealtime(topic, {
          ...data,
          timestamp: normalizedTimestamp,
          timestamp_ms: normalizedTimestampMs,
        }, 'emg');
        return;
      }

      await saveMotionPayload(data, normalizedTimestamp, normalizedTimestampMs);
      console.log(`[MQTT] Saved motion | device: ${data.device_id} | ts: ${normalizedTimestamp}`);

      broadcastRealtime(topic, {
        ...data,
        timestamp: normalizedTimestamp,
        timestamp_ms: normalizedTimestampMs,
      }, 'motion');
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
