const pool = require('../db');
const { mapEmgRow } = require('../utils/mapRow');

const DEVICE_ID = process.env.DEVICE_ID || 'health_device';

const getLatestEmg = async (req, res) => {
  const device_id = req.query.device_id || DEVICE_ID;

  try {
    const result = await pool.query(
      `SELECT * FROM emg_data
       WHERE device_id = $1
       ORDER BY timestamp DESC
       LIMIT 1`,
      [device_id],
    );

    if (result.rows.length === 0) return res.json(null);
    return res.json(mapEmgRow(result.rows[0]));
  } catch (err) {
    console.error('[emg/latest]', err.message);
    return res.status(500).json({ message: 'Loi truy van du lieu EMG.' });
  }
};

const getEmgHistory = async (req, res) => {
  const device_id = req.query.device_id || DEVICE_ID;
  const limit = Math.min(parseInt(req.query.limit, 10) || 60, 300);

  try {
    const result = await pool.query(
      `SELECT * FROM (
         SELECT * FROM emg_data
         WHERE device_id = $1
         ORDER BY timestamp DESC
         LIMIT $2
       ) sub
       ORDER BY timestamp ASC`,
      [device_id, limit],
    );

    return res.json(result.rows.map(mapEmgRow));
  } catch (err) {
    console.error('[emg/history]', err.message);
    return res.status(500).json({ message: 'Loi truy van lich su EMG.' });
  }
};

module.exports = { getLatestEmg, getEmgHistory };