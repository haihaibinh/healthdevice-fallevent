const pool = require('../db');

const DEVICE_ID = process.env.DEVICE_ID || 'health_device';

// GET /api/device
const getDevice = async (req, res) => {
  try {
    // Lấy thời gian ghi dữ liệu gần nhất để xác định lastSeen
    const result = await pool.query(
      `SELECT timestamp FROM sensor_data
       WHERE device_id = $1
       ORDER BY timestamp DESC
       LIMIT 1`,
      [DEVICE_ID]
    );

    const lastTimestamp = result.rows[0]?.timestamp;
    const lastSeen      = lastTimestamp
      ? new Date(Number(lastTimestamp) * 1000).toISOString()
      : null;

    // Coi là online nếu có dữ liệu trong vòng 10 giây
    const isOnline = lastTimestamp
      ? (Date.now() / 1000 - Number(lastTimestamp)) < 10
      : false;

    res.json({
      id:         DEVICE_ID,
      name:       process.env.DEVICE_NAME || 'HealthBand Pro',
      macAddress: process.env.DEVICE_MAC  || 'AA:BB:CC:DD:EE:FF',
      status:     isOnline ? 'CONNECTED' : 'DISCONNECTED',
      isOnline,
      lastSeen,
    });
  } catch (err) {
    console.error('[device]', err.message);
    res.status(500).json({ message: 'Lỗi truy vấn thiết bị.' });
  }
};

module.exports = { getDevice };
