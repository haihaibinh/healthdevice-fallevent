const router = require('express').Router();
const { getDevice } = require('../controllers/device.controller');

router.get('/', getDevice);

module.exports = router;
