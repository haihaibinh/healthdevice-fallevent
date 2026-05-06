const pool = require('../db');
const { mapNormalRow, mapFallEventRow } = require('../utils/mapRow');

const DEVICE_ID = process.env.DEVICE_ID || 'health_device';
const DEFAULT_FALL_EVENT_WINDOW_SECONDS = Number(process.env.LATEST_FALL_WINDOW_SECONDS) || 10;

const getLatest = async (req, res) => {
  const device_id = req.query.device_id || DEVICE_ID;

  try {
    const result = await pool.query(
      `SELECT * FROM sensor_data
       WHERE device_id = $1
       ORDER BY timestamp DESC
       LIMIT 1`,
      [device_id],
    );

    if (result.rows.length === 0) return res.json(null);
    return res.json(mapNormalRow(result.rows[0]));
  } catch (err) {
    console.error('[sensor/latest]', err.message);
    return res.status(500).json({ message: 'Loi truy van du lieu.' });
  }
};

const getHistory = async (req, res) => {
  const device_id = req.query.device_id || DEVICE_ID;
  const limit = Math.min(parseInt(req.query.limit, 10) || 100, 500);

  try {
    const result = await pool.query(
      `SELECT * FROM (
         SELECT * FROM sensor_data
         WHERE device_id = $1
         ORDER BY timestamp DESC
         LIMIT $2
       ) sub
       ORDER BY timestamp ASC`,
      [device_id, limit],
    );

    return res.json(result.rows.map(mapNormalRow));
  } catch (err) {
    console.error('[sensor/history]', err.message);
    return res.status(500).json({ message: 'Loi truy van lich su.' });
  }
};

const getFallEvents = async (req, res) => {
  const device_id = req.query.device_id || DEVICE_ID;

  try {
    const result = await pool.query(
      `SELECT * FROM sensor_data
       WHERE device_id = $1
         AND event IN (1, 2)
       ORDER BY timestamp DESC
       LIMIT 20`,
      [device_id],
    );

    return res.json(result.rows.map(mapFallEventRow));
  } catch (err) {
    console.error('[sensor/fall-events]', err.message);
    return res.status(500).json({ message: 'Loi truy van su kien.' });
  }
};

const getLatestFall = async (req, res) => {
  const device_id = req.query.device_id || DEVICE_ID;
  const since = Number(req.query.since);
  const cutoff = Number.isFinite(since)
    ? since
    : Math.floor(Date.now() / 1000) - DEFAULT_FALL_EVENT_WINDOW_SECONDS;

  try {
    const result = await pool.query(
      `SELECT * FROM sensor_data
       WHERE device_id = $1
         AND event IN (1, 2)
         AND timestamp > $2
       ORDER BY timestamp DESC
       LIMIT 1`,
      [device_id, cutoff],
    );

    if (result.rows.length === 0) return res.json(null);
    return res.json(mapFallEventRow(result.rows[0]));
  } catch (err) {
    console.error('[sensor/latest-fall]', err.message);
    return res.status(500).json({ message: 'Loi truy van su kien.' });
  }
};

module.exports = { getLatest, getHistory, getFallEvents, getLatestFall };
