import { useCallback, useEffect, useState } from 'react';
import client from '../api/client';
import DataTable from '../components/DataTable';
import StatusBadge from '../components/StatusBadge';
import PageHeader from '../components/PageHeader';
import { rows, apiError, utcTime } from '../tracking';
const blank = { status: '', device_id: '', from_date: '', to_date: '' };
const columns = [
  { key: 'violation_id', label: 'Violation ID', mono: true },
  { key: 'device_id', label: 'Device', mono: true },
  { key: 'speed_kmh', label: 'Speed', render: v => <span className="danger-text">{v} km/h</span> },
  { key: 'speed_limit_kmh', label: 'Limit', render: v => `${v} km/h` },
  { key: 'excess_kmh', label: 'Excess', render: v => `+${v} km/h` },
  { key: 'fine_amount_egp', label: 'Fine', render: v => `${v} EGP` },
  { key: 'fine_status', label: 'Status', render: v => <StatusBadge status={v} /> },
  { key: 'duration_sec', label: 'Duration', render: v => v == null ? '—' : `${v}s` },
  { key: 'received_at', label: 'Received at', render: v => v ? new Date(utcTime(v)).toLocaleString() : '—' },
];
export default function Violations() {
  const [data, setData] = useState([]);
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState('');
  const [busy, setBusy] = useState(null);
  const [draft, setDraft] = useState(blank);
  const [filters, setFilters] = useState(blank);
  const fetchData = useCallback(async signal => {
    setLoading(true); setError('');
    try {
      const params = { limit: 50, offset: 0, ...Object.fromEntries(Object.entries(filters).filter(([, v]) => v)) };
      const r = await client.get('/violations', { params, signal });
      if (!signal?.aborted) setData(rows(r.data, 'violations'));
    } catch (e) { if (!signal?.aborted) setError(apiError(e)); }
    finally { if (!signal?.aborted) setLoading(false); }
  }, [filters]);
  useEffect(() => {
    const controller = new AbortController(); fetchData(controller.signal);
    return () => controller.abort();
  }, [fetchData]);
  async function updateStatus(id, status) {
    setBusy(id); setError('');
    try { await client.put(`/violations/${encodeURIComponent(id)}/status`, { status }); await fetchData(); }
    catch (e) { setError(apiError(e)); }
    finally { setBusy(null); }
  }
  const field = key => ({ value: draft[key], onChange: e => setDraft(d => ({ ...d, [key]: e.target.value })) });
  return <div><PageHeader title="Violations" subtitle={`${data.length} records shown · filter and manage fine status`} />
    <form className="panel filter-bar" onSubmit={e => { e.preventDefault(); setFilters({ ...draft }); }}>
      <label>Status<select {...field('status')}><option value="">All statuses</option>{['pending', 'paid', 'cancelled', 'disputed'].map(s => <option key={s}>{s}</option>)}</select></label>
      <label>Device ID<input {...field('device_id')} placeholder="All devices" /></label>
      <label>From<input type="date" {...field('from_date')} /></label>
      <label>To<input type="date" {...field('to_date')} /></label>
      <button type="submit">Search</button><button type="button" onClick={() => { setDraft(blank); setFilters({ ...blank }); }}>Clear</button>
    </form>
    {error && <div className="error-panel" role="alert">{error}</div>}
    <DataTable columns={columns} data={data} loading={loading} actions={row => row.fine_status === 'pending' ? <div className="row-actions"><button disabled={busy != null} onClick={() => updateStatus(row.violation_id, 'paid')}>{busy === row.violation_id ? 'Updating…' : 'Paid'}</button><button disabled={busy != null} onClick={() => updateStatus(row.violation_id, 'cancelled')}>Cancel</button></div> : '—'} />
  </div>;
}
