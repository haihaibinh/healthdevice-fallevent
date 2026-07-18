const router = require('express').Router();
const {
  getLatestEmg,
  getEmgHistory,
} = require('../controllers/emg.controller');

router.get('/latest', getLatestEmg);
router.get('/history', getEmgHistory);

module.exports = router;