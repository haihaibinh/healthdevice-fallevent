const express = require('express');
const cors = require('cors');
require('dotenv').config();

const deviceRoutes = require('./src/routes/device.routes');
const emgRoutes = require('./src/routes/emg.routes');
const sensorRoutes = require('./src/routes/sensor.routes');

const app = express();

app.use(cors({ origin: process.env.FRONTEND_ORIGIN || 'http://localhost:5173' }));
app.use(express.json());

app.get('/health', async (req, res) => {
  res.json({
    status: 'ok',
    service: 'healthtrack-backend',
    timestamp: new Date().toISOString(),
  });
});

app.use('/api/device', deviceRoutes);
app.use('/api/emg', emgRoutes);
app.use('/api/sensor', sensorRoutes);

module.exports = app;
