import api from './api';

const deviceService = {
  getMyDevice: async () => {
    const res = await api.get('/device');
    return res.data;
    // Backend trả về: { id: 'health_device', name, macAddress, status, isOnline, lastSeen }
  },

  // Chưa có endpoint thật → giữ mock
  registerDevice: async (name, macAddress) => {
    return {
      id:         'health_device',
      name,
      macAddress,
      status:     'CONNECTED',
      isOnline:   true,
      lastSeen:   new Date().toISOString(),
    };
  },

  deleteDevice: async () => null,
};

export default deviceService;