import axios from 'axios';
import toast from '../utils/toast';

const BASE_URL = import.meta.env.VITE_API_URL || 'http://localhost:3000/api';
const TOKEN_KEY = 'ht_token';

export const getToken = () => localStorage.getItem(TOKEN_KEY);
export const setToken = (token) => localStorage.setItem(TOKEN_KEY, token);
export const removeToken = () => localStorage.removeItem(TOKEN_KEY);

const api = axios.create({
  baseURL: BASE_URL,
  headers: { 'Content-Type': 'application/json' },
  timeout: 10000,
});

api.interceptors.request.use((config) => {
  const token = getToken();
  if (token) config.headers.Authorization = `Bearer ${token}`;
  return config;
});

api.interceptors.response.use(
  (res) => res,
  (err) => {
    const status = err.response?.status;
    const message = err.response?.data?.message;

    if (status === 401) {
      removeToken();
      window.location.href = '/login';
      return Promise.reject(err);
    }

    if (status === 403) {
      toast.error('Bạn không có quyền thực hiện thao tác này.');
      return Promise.reject(err);
    }

    if (status >= 500) {
      toast.error('Lỗi máy chủ. Vui lòng thử lại sau.');
      return Promise.reject(err);
    }

    if (message) toast.error(message);
    return Promise.reject(err);
  }
);

export default api;