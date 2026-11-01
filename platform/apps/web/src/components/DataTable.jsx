export default function DataTable({ columns, data, loading, actions }) {
  if (loading) {
    return (
      <div
        style={{
          background: 'var(--bg-card)',
          border: '1px solid var(--border)',
          borderRadius: '10px',
          padding: '48px',
          textAlign: 'center',
          color: 'var(--text-muted)',
          fontFamily: 'JetBrains Mono, monospace',
          fontSize: '12px',
          letterSpacing: '0.1em',
        }}
      >
        <div style={{ marginBottom: '12px', fontSize: '22px' }}>⟳</div>
        LOADING...
      </div>
    );
  }

  if (!data || data.length === 0) {
    return (
      <div
        style={{
          background: 'var(--bg-card)',
          border: '1px solid var(--border)',
          borderRadius: '10px',
          padding: '48px',
          textAlign: 'center',
          color: 'var(--text-muted)',
          fontFamily: 'JetBrains Mono, monospace',
          fontSize: '12px',
          letterSpacing: '0.1em',
        }}
      >
        <div style={{ marginBottom: '12px', fontSize: '22px', opacity: 0.4 }}>◻</div>
        NO RECORDS FOUND
      </div>
    );
  }

  const allCols = actions ? [...columns, { key: '__actions', label: 'Action' }] : columns;

  return (
    <div
      style={{
        background: 'var(--bg-card)',
        border: '1px solid var(--border)',
        borderRadius: '10px',
        overflow: 'hidden',
      }}
    >
      <div style={{ overflowX: 'auto' }}>
        <table style={{ width: '100%', borderCollapse: 'collapse' }}>
          <thead>
            <tr style={{ borderBottom: '1px solid var(--border-bright)' }}>
              {allCols.map((col) => (
                <th
                  key={col.key}
                  style={{
                    padding: '12px 16px',
                    textAlign: 'left',
                    fontFamily: 'JetBrains Mono, monospace',
                    fontSize: '10px',
                    fontWeight: 600,
                    color: 'var(--text-muted)',
                    letterSpacing: '0.12em',
                    textTransform: 'uppercase',
                    whiteSpace: 'nowrap',
                    background: 'var(--table-heading)',
                  }}
                >
                  {col.label}
                </th>
              ))}
            </tr>
          </thead>
          <tbody>
            {data.map((row, i) => (
              <tr
                key={i}
                className="table-row-hover"
                style={{
                  borderBottom: '1px solid var(--border)',
                  background: i % 2 === 0 ? 'transparent' : 'var(--bg-row-alt)',
                }}
              >
                {columns.map((col) => (
                  <td
                    key={col.key}
                    style={{
                      padding: '11px 16px',
                      fontFamily: col.mono ? 'JetBrains Mono, monospace' : 'Sora, sans-serif',
                      fontSize: col.mono ? '11px' : '13px',
                      color: col.dim ? 'var(--text-secondary)' : 'var(--text-primary)',
                      whiteSpace: 'nowrap',
                    }}
                  >
                    {col.render ? col.render(row[col.key], row) : (row[col.key] ?? '—')}
                  </td>
                ))}
                {actions && (
                  <td style={{ padding: '11px 16px' }}>
                    {actions(row)}
                  </td>
                )}
              </tr>
            ))}
          </tbody>
        </table>
      </div>
    </div>
  );
}
