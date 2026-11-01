import { useEffect, useState } from 'react';
import client from '../api/client';
import DataTable from '../components/DataTable';
import PageHeader from '../components/PageHeader';
import { rows, apiError, utcTime } from '../tracking';
const columns = [
  { key: 'id', label: 'ID', mono: true },
  { key: 'full_name', label: 'Full name' },
  { key: 'national_id', label: 'National ID', mono: true },
  { key: 'car_plate', label: 'Car plate', mono: true },
  { key: 'phone', label: 'Phone', mono: true },
  { key: 'total_fines_egp', label: 'Total fines', render: v => <span className={v > 0 ? 'danger-text' : 'muted'}>{Number(v || 0).toFixed(0)} EGP</span> },
  { key: 'registered_at', label: 'Registered', render: v => v ? new Date(utcTime(v)).toLocaleString() : '—' },
  { key: 'is_active', label: 'Status', render: v => v ? 'Active' : 'Inactive' },
];
export default function Drivers() {
  const [data, setData] = useState([]);
  const [error, setError] = useState('');
  const [loading, setLoading] = useState(true);
  const [retry, setRetry] = useState(0);
  useEffect(() => {
    const controller = new AbortController();
    setLoading(true); setError('');
    client.get('/drivers?limit=50&offset=0', { signal: controller.signal }).then(r => setData(rows(r.data, 'drivers'))).catch(e => { if (!controller.signal.aborted) setError(apiError(e)); }).finally(() => { if (!controller.signal.aborted) setLoading(false); });
    return () => controller.abort();
  }, [retry]);
  return <div><PageHeader title="Drivers" subtitle={`${data.length} drivers shown · registration and fines`}><button onClick={() => setRetry(r => r + 1)}>Refresh</button></PageHeader>{error && <div className="error-panel" role="alert">{error}</div>}<DataTable columns={columns} data={data} loading={loading} /></div>;
}
