import axios from 'axios';

// The website is an admin tool: every call goes to /v1/admin and the browser
// sends the admin Basic credentials. /v1/app is for the driver app only.
export const apiOrigin = (import.meta.env.VITE_API_BASE_URL || '').replace(/\/+$/, '');
const options = { timeout: 10000, withCredentials: true };
const client = axios.create({ ...options, baseURL: `${apiOrigin}/v1/admin` });
export const speedLimitsUrl = bbox => `${apiOrigin}/v1/speed-limits?bbox=${bbox}`;
export default client;
