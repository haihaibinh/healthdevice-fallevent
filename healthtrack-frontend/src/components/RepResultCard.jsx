export default function RepResultCard({ rep, targetActivationPct }) {
  if (!rep) return null;

  return (
    <div className="card">
      <div className="card-body stack">
        <div className="section-heading">
          <div>
            <h2>Kết quả rep</h2>
            <p className="card-subtitle">Đánh giá rep vừa hoàn thành dựa trên chu kỳ kích hoạt và thả lỏng cơ đùi.</p>
          </div>
          <span className="chip" data-tone="success">{rep.score}/100</span>
        </div>

        <div className="summary-grid">
          <div className="summary-item"><strong>Peak RMS:</strong> {rep.peakRms?.toFixed?.(3) ?? '--'}</div>
          <div className="summary-item"><strong>Peak activation:</strong> {rep.peakActivationPct?.toFixed?.(0) ?? '--'}%</div>
          <div className="summary-item"><strong>Thời gian đứng lên:</strong> {rep.standUpDurationMs != null ? `${Math.round(rep.standUpDurationMs)} ms` : '--'}</div>
          <div className="summary-item"><strong>Thả lỏng:</strong> {rep.relaxationSuccess ? 'Thành công' : 'Chưa ổn định'}</div>
        </div>

        <div className="feedback-list">
          {rep.feedback?.length ? rep.feedback.map((item) => (
            <div key={item} className="feedback-item">{item}</div>
          )) : <div className="feedback-item">Hãy kích hoạt cơ đùi đều hơn để đạt mục tiêu {targetActivationPct}%.</div>}
        </div>
      </div>
    </div>
  );
}