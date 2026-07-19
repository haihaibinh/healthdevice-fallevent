export default function GameControls({ state, onPause, onResume, onStop }) {
  return (
    <div className="card">
      <div className="card-body stack">
        <div className="section-heading">
          <div>
            <h2>Điều khiển</h2>
            <p className="card-subtitle">Tạm dừng hoặc dừng hẳn phiên tập khi cần.</p>
          </div>
        </div>

        <div className="controls-grid">
          <button type="button" className="button-ghost" onClick={state.paused ? onResume : onPause}>
            {state.paused ? 'Tiếp tục' : 'Pause'}
          </button>
          <button type="button" className="button-danger" onClick={onStop}>Stop</button>
        </div>

        <div className="summary-item">
          <strong>Trạng thái:</strong> {state.status}
        </div>
      </div>
    </div>
  );
}