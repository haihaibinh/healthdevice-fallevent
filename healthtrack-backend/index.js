require('dotenv').config();

const app = require('./server');
const { startMqttSubscriber } = require('./src/mqtt');

const PORT = Number(process.env.PORT) || 3000;

app.listen(PORT, () => {
  console.log(`Server running at http://localhost:${PORT}`);
  startMqttSubscriber();
});
