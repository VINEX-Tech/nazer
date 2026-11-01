const badgeStyles = {
  pending:   { bg: 'var(--warning-soft)',  border: 'var(--warning-border)',  color: 'var(--amber)', label: 'PENDING',   pulse: true  },
  paid:      { bg: 'var(--success-soft)',  border: 'var(--success-border)', color: 'var(--green)', label: 'PAID',      pulse: false },
  cancelled: { bg: 'var(--neutral-soft)', border: 'var(--neutral-border)', color: 'var(--text-secondary)', label: 'CANCELLED', pulse: false },
  disputed:  { bg: 'var(--danger-soft)',  border: 'var(--danger-border)', color: 'var(--red)', label: 'DISPUTED',  pulse: false },
};

export default function StatusBadge({ status }) {
  const s = badgeStyles[status] || badgeStyles.pending;

  return (
    <span
      className={s.pulse ? 'badge-pulse' : ''}
      style={{
        display: 'inline-flex',
        alignItems: 'center',
        gap: '5px',
        padding: '3px 9px',
        borderRadius: '4px',
        background: s.bg,
        border: `1px solid ${s.border}`,
        color: s.color,
        fontFamily: 'JetBrains Mono, monospace',
        fontSize: '10px',
        fontWeight: 600,
        letterSpacing: '0.08em',
      }}
    >
      <span
        style={{
          width: '5px',
          height: '5px',
          borderRadius: '50%',
          background: s.color,
          flexShrink: 0,
        }}
      />
      {s.label}
    </span>
  );
}
