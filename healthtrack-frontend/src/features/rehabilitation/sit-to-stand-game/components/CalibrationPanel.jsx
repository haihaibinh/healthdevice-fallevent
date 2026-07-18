export default function CalibrationPanel({ state, onStartRestCalibration, onStartReferenceCalibration, onStartSession }) {
  const restReady = Number.isFinite(state.restRms);
  const referenceReady = Number.isFinite(state.referenceRms) && state.referenceRms > state.restRms;

  return (
    <div className="card">
      <div className="card-body stack">
        <div className="section-heading">
          <div>
            <h2>Hiệu chuẩn</h2>
            <p className="card-subtitle">Cần đo baseline nghỉ và mức tham chiếu đứng lên trước khi bắt đầu game.</p>
          </div>
        </div>

        <div className="controls-grid">
          <button type="button" className="button" onClick={onStartRestCalibration}>Bắt đầu hiệu chuẩn nghỉ</button>
          <button type="button" className="button-ghost" onClick={onStartReferenceCalibration} disabled={!restReady}>Bắt đầu hiệu chuẩn tham chiếu</button>
          <button type="button" className="button" onClick={onStartSession} disabled={!restReady || !referenceReady}>Bắt đầu game</button>
          <div className="button-ghost" style={{ display: 'flex', alignItems: 'center', justifyContent: 'center' }}>
            {referenceReady ? 'Đã sẵn sàng' : 'Chưa đủ dữ liệu'}
          </div>
        </div>

        <div className="calibration-list">
          <div className="calibration-item">
            <strong>Rest RMS:</strong> {restReady ? state.restRms.toFixed(3) : '--'}
          </div>
          <div className="calibration-item">
            <strong>Rest STD:</strong> {Number.isFinite(state.restStd) ? state.restStd.toFixed(3) : '--'}
          </div>
          <div className="calibration-item">
            <strong>Reference RMS:</strong> {referenceReady ? state.referenceRms.toFixed(3) : '--'}
          </div>
          <div className="calibration-item">
            <strong>Progress hiệu chuẩn:</strong> {state.status}
          </div>
        </div>
      </div>
    </div>
  );
}