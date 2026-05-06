const router = require('express').Router();
const {
  getLatest,
  getHistory,
  getFallEvents,
  getLatestFall,
} = require('../controllers/sensor.controller');

router.get('/latest',       getLatest);
router.get('/history',      getHistory);
router.get('/fall-events',  getFallEvents);
router.get('/latest-fall',  getLatestFall);

module.exports = router;
