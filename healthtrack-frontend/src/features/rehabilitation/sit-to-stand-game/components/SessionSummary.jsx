import { clamp } from '../utils/normalization.js';

export default function SessionSummary({ summary, repResults }) {
  if (!summary) return null;

  const chartValues = Array.isArray(repResults) ? repResults : [];

  return (
    <div className="card">
      <div className="card-body stack">
        <div className="section-heading">
          <div>
            <h2>Tổng kết session</h2>
            <p className="card-subtitle">Hiển thị kết quả theo rep, không dùng để kết luận chẩn đoán hoặc phục hồi lâm sàng.</p>
          </div>
        </div>

        <div className="summary-grid">
          <div className="summary-item"><strong>Số rep dự kiến:</strong> {summary.plannedReps}</div>
          <div className="summary-item"><strong>Số rep hoàn thành:</strong> {summary.completedReps}</div>
          <div className="summary-item"><strong>Điểm trung bình:</strong> {summary.averageScore.toFixed(1)}</div>
          <div className="summary-item"><strong>Rep tốt nhất:</strong> {summary.bestRep ? `#${summary.bestRep.repIndex} (${summary.bestRep.score})` : '--'}</div>
          <div className="summary-item"><strong>Thời gian đứng lên TB:</strong> {summary.avgStandUpDurationMs ? `${Math.round(summary.avgStandUpDurationMs)} ms` : '--'}</div>
          <div className="summary-item"><strong>Peak activation TB:</strong> {summary.avgPeakActivationPct.toFixed(1)}%</div>
          <div className="summary-item"><strong>Tỷ lệ thả lỏng:</strong> {(summary.relaxationSuccessRate * 100).toFixed(0)}%</div>
          <div className="summary-item"><strong>Ghi chú:</strong> Chỉ dùng RMS để theo dõi hoạt động cơ đùi.</div>
        </div>

        <div>
          <div className="stat-label" style={{ marginBottom: 10 }}>Biểu đồ điểm theo rep</div>
          <div className="chart-bars">
            {chartValues.map((rep) => (
              <div key={rep.repIndex} className="chart-bar-wrap">
                <div className="chart-bar" style={{ height: `${clamp((rep.score || 0) * 1.15, 16, 100)}px` }} />
                <span className="chart-label">{rep.repIndex}</span>
              </div>
            ))}
          </div>
        </div>

        <div>
          <div className="stat-label" style={{ marginBottom: 10 }}>Biểu đồ thời gian đứng lên theo rep</div>
          <div className="chart-bars">
            {chartValues.map((rep) => (
              <div key={`${rep.repIndex}-duration`} className="chart-bar-wrap">
                <div className="chart-bar" style={{ height: `${clamp((rep.standUpDurationMs || 0) / 45, 16, 120)}px`, background: 'linear-gradient(180deg, rgba(245, 158, 11, 0.92), rgba(248, 113, 113, 0.82))' }} />
                <span className="chart-label">{rep.repIndex}</span>
              </div>
            ))}
          </div>
        </div>
      </div>
    </div>
  );
}