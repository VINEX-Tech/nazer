export default function StatCard({ title, value, subtitle }) {
  return <div className="stat-card"><div className="muted">{title}</div><strong>{value ?? '—'}</strong>{subtitle && <p className="muted">{subtitle}</p>}</div>;
}
