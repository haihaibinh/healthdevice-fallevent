const Aedes = require('aedes');
const net = require('net');
const mqtt = require('mqtt');
const { WebSocketServer } = require('ws');
const pool = require('./db');

const aedes = new Aedes();
const MQTT_PORT = parseInt(process.env.MQTT_PORT, 10) || 1883;
const WS_PORT = parseInt(process.env.WS_PORT, 10) || 3001;
const MQTT_TOPIC = process.env.MQTT_TOPIC || 'health/device/health_device';

const wsClients = new Set();
let started = false;

function normalizeTimestamp(value) {
  const numericValue = Number(value);
  const currentUnixTime = Math.floor(Date.now() / 1000);

  if (!Number.isFinite(numericValue)) return currentUnixTime;
  if (numericValue < 1700000000) return currentUnixTime;
  if (numericValue > currentUnixTime + 86400) return currentUnixTime;

  return Math.floor(numericValue);
}

function startMqttSubscriber() {
  if (started) return;
  started = true;

  const broker = net.createServer(aedes.handle);
  broker.listen(MQTT_PORT, () => {
    console.log(`MQTT broker listening on ${MQTT_PORT}`);
  });

  const wss = new WebSocketServer({ port: WS_PORT });
  wss.on('connection', (ws) => {
    wsClients.add(ws);

    ws.on('close', () => {
      wsClients.delete(ws);
    });

    ws.on('error', () => {
      wsClients.delete(ws);
    });
  });

  console.log(`WebSocket server listening on ${WS_PORT}`);

  const client = mqtt.connect(`mqtt://localhost:${MQTT_PORT}`);

  client.on('connect', () => {
    console.log('MQTT subscriber connected');
    client.subscribe(MQTT_TOPIC, (err) => {
      if (err) {
        console.error('MQTT subscribe error:', err.message);
      }
    });
  });

  client.on('message', async (topic, message) => {
    try {
      const data = JSON.parse(message.toString());
      const normalizedTimestamp = normalizeTimestamp(data.timestamp);

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
          data.physics?.acceleration ?? null,
          data.physics?.emg ?? null,
          data.physics?.angle ?? null,
          data.biometric?.heart_rate ?? null,
          data.biometric?.spo2 ?? null,
          data.biometric?.hrv ?? null,
          data.risk_score ?? null,
          data.event ?? null,
        ],
      );

      const payload = JSON.stringify({
        topic,
        data: {
          ...data,
          timestamp: normalizedTimestamp,
        },
      });
      for (const ws of wsClients) {
        if (ws.readyState === ws.OPEN) {
          ws.send(payload);
        }
      }
    } catch (err) {
      console.error('[MQTT] Error:', err.message);
    }
  });

  client.on('error', (err) => {
    console.error('MQTT error:', err.message);
  });

  aedes.on('client', (mqttClient) => console.log('[Broker] Connected:', mqttClient.id));
  aedes.on('clientDisconnect', (mqttClient) => console.log('[Broker] Disconnected:', mqttClient.id));
}

module.exports = { startMqttSubscriber };
