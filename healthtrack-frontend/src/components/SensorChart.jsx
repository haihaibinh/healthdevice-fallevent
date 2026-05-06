import { useMemo } from 'react';
import {
  CartesianGrid,
  Line,
  LineChart,
  ReferenceLine,
  ResponsiveContainer,
  Tooltip,
  XAxis,
  YAxis,
} from 'recharts';
import { COLORS, SENSOR_COLORS } from '../constants/theme';

function formatTimestamp(timestamp) {
  if (!timestamp) return '--:--:--';
  return new Date(timestamp * 1000).toLocaleTimeString('vi-VN', {
    hour: '2-digit',
    minute: '2-digit',
    second: '2-digit',
    timeZone: 'Asia/Ho_Chi_Minh',
    hour12: false,
  });
}

function CustomTooltip({ active, payload, label, unit }) {
  if (!active || !payload?.length) return null;

  return (
    <div
      style={{
        background: 'rgba(2, 6, 23, 0.92)',
        border: `1px solid ${COLORS.border}`,
        borderRadius: 16,
        padding: '10px 12px',
        boxShadow: '0 18px 36px rgba(2, 6, 23, 0.32)',
      }}
    >
      <div style={{ color: COLORS.textMuted, marginBottom: 6, fontSize: 12 }}>{label}</div>
      {payload.map((entry) => (
        <div key={entry.dataKey} style={{ color: entry.color, fontWeight: 700, fontSize: 13 }}>
          {entry.value != null ? Number(entry.value).toFixed(1) : '--'} {unit}
        </div>
      ))}
    </div>
  );
}

export default function SensorChart({
  data = [],
  dataKey = 'hr',
  color,
  unit = '',
  height = 180,
  referenceLines = [],
  yDomain = ['auto', 'auto'],
  showGrid = true,
}) {
  const resolvedColor = color || SENSOR_COLORS[dataKey] || COLORS.primary;

  const chartData = useMemo(
    () =>
      data.map((entry) => ({
        time: formatTimestamp(entry.timestamp),
        value: entry[dataKey] ?? null,
      })),
    [data, dataKey],
  );

  return (
    <ResponsiveContainer width="100%" height={height}>
      <LineChart data={chartData} margin={{ top: 12, right: 8, left: -20, bottom: 0 }}>
        {showGrid ? (
          <CartesianGrid strokeDasharray="4 6" stroke={COLORS.border} vertical={false} opacity={0.7} />
        ) : null}
        <XAxis
          dataKey="time"
          tick={{ fill: COLORS.textMuted, fontSize: 11 }}
          tickLine={false}
          axisLine={false}
          minTickGap={26}
        />
        <YAxis
          domain={yDomain}
          tick={{ fill: COLORS.textMuted, fontSize: 11 }}
          tickLine={false}
          axisLine={false}
          width={44}
        />
        <Tooltip content={<CustomTooltip unit={unit} />} cursor={{ stroke: `${resolvedColor}55`, strokeWidth: 1 }} />
        {referenceLines.map((line) => (
          <ReferenceLine
            key={`${line.label}-${line.y}`}
            y={line.y}
            stroke={line.color || COLORS.warning}
            strokeDasharray="6 6"
            label={{ value: line.label, fill: line.color || COLORS.warning, fontSize: 11 }}
          />
        ))}
        <Line
          type="monotone"
          dataKey="value"
          stroke={resolvedColor}
          strokeWidth={3}
          dot={false}
          activeDot={{ r: 5, fill: resolvedColor, stroke: '#020617', strokeWidth: 2 }}
          isAnimationActive={false}
          connectNulls={false}
        />
      </LineChart>
    </ResponsiveContainer>
  );
}
