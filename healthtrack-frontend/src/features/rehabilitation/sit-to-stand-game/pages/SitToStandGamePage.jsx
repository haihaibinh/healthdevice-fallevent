import React from 'react';
import Navbar from '../../../../components/Navbar.jsx';
import { ROUTES } from '../../../../constants/theme.js';
import { useDevice } from '../../../../contexts/DeviceContext.jsx';
import { useSensorData } from '../../../../hooks/useSensorData.js';
import { DEFAULT_SIT_TO_STAND_SETTINGS, SIT_TO_STAND_INSTRUCTIONS } from '../constants/sitToStandGameConfig.js';
import RepResultCard from '../components/RepResultCard.jsx';
import SessionSummary from '../components/SessionSummary.jsx';
import SitToStandHillScene from '../components/SitToStandHillScene.jsx';
import { useSitToStandGame } from '../hooks/useSitToStandGame.js';

export default function SitToStandGamePage() {
  const { device } = useDevice();
  const { latestEmg } = useSensorData(device?.id);
  const { 
    state, 
    realtime, 
    sessionSummary, 
    startRestCalibration, 
    startReferenceCalibration, 
    startSession, 
    pauseGame, 
    resumeGame, 
    stopSession, 
    restartFromConnection 
  } = useSitToStandGame(latestEmg, DEFAULT_SIT_TO_STAND_SETTINGS);

  const repCount = state.repIndex || state.repResults.length;
  const currentRep = state.currentRep || state.repResults[state.repResults.length - 1] || null;
  const instruction = SIT_TO_STAND_INSTRUCTIONS[state.status] || state.message || SIT_TO_STAND_INSTRUCTIONS.IDLE;

  // Tính độ biến thiên tín hiệu EMG (độ lệch chuẩn Standard Deviation) từ 50 mẫu raw gần nhất
  const emgRawList = latestEmg?.emg_raw_list || [];
  let emgVariation = 0;
  if (emgRawList.length > 0) {
    const mean = emgRawList.reduce((sum, val) => sum + val, 0) / emgRawList.length;
    const variance = emgRawList.reduce((sum, val) => sum + Math.pow(val - mean, 2), 0) / emgRawList.length;
    emgVariation = Math.sqrt(variance);
  }

  return (
    <>
      <main className="rehab-shell" style={{ maxWidth: '100%', padding: '24px 32px 120px', margin: '0 auto' }}>
        
        {/* Header trang gọn gàng */}
        <header className="page-header" style={{ marginBottom: '24px', display: 'flex', justifyContent: 'space-between', alignItems: 'flex-start', flexWrap: 'wrap', gap: '16px' }}>
          <div>
            <span className="page-eyebrow" style={{ color: '#38bdf8', fontSize: '0.85rem', fontWeight: 'bold', textTransform: 'uppercase', letterSpacing: '0.08em' }}>Phục hồi chức năng</span>
            <h1 className="page-title" style={{ fontSize: '2rem', fontWeight: '800', color: '#fff', margin: '4px 0 8px' }}>Endless Rehab Runner</h1>
            <p className="page-subtitle" style={{ color: '#94a3b8', fontSize: '0.95rem', margin: 0 }}>
              Game chạy ngang vô hạn (Flappy Bird) điều khiển bằng việc co cơ đùi nhận dữ liệu EMG realtime.
            </p>
          </div>

          <div className="header-status" style={{ display: 'flex', gap: '12px', alignItems: 'center' }}>
            <div className="status-pill" data-tone={device ? 'success' : 'warning'}>
              <span className="status-dot" />
              {device ? `${device.name} đã kết nối` : 'Chưa ghép đôi thiết bị'}
            </div>
            <div className="status-pill" data-tone={realtime.isStale ? 'danger' : 'success'}>
              EMG {realtime.isStale ? 'mất kết nối' : 'trực tuyến'}
            </div>
          </div>
        </header>

        {/* CONTAINER CHỨA GAME CANVAS VÀ HUD OVERLAY (FULL WIDTH) */}
        <div className="game-container-wrapper" style={{ position: 'relative', width: '100%', borderRadius: '24px', overflow: 'hidden', boxShadow: '0 25px 50px -12px rgba(0, 0, 0, 0.6)', border: '1px solid rgba(255, 255, 255, 0.08)', background: '#090d16', minHeight: '760px' }}>
          
          {/* Game Canvas */}
          <SitToStandHillScene
            rms={state.smoothedRms}
            threshold={state.settings.simpleRmsThreshold}
            releaseThreshold={state.settings.contractionReleaseThreshold}
            repIndex={state.repIndex}
            maxReps={state.settings.repetitions}
            status={state.status}
            instruction={instruction}
          />

          {/* HUD OVERLAY 1: GÓC TRÊN TRÁI - SỐ REP & ĐIỂM SỐ */}
          <div className="hud-panel" style={{ position: 'absolute', top: '20px', left: '20px', background: 'rgba(15, 23, 42, 0.7)', backdropFilter: 'blur(12px)', border: '1px solid rgba(255, 255, 255, 0.1)', padding: '12px 18px', borderRadius: '16px', display: 'flex', flexDirection: 'column', gap: '4px', pointerEvents: 'none', boxShadow: '0 10px 25px rgba(0,0,0,0.3)', zIndex: 5 }}>
            <div style={{ fontSize: '0.75rem', textTransform: 'uppercase', letterSpacing: '0.08em', color: '#94a3b8', fontWeight: 'bold' }}>Tiến Trình Tập</div>
            <div style={{ fontSize: '1.8rem', fontWeight: '900', color: '#34d399', textShadow: '0 0 10px rgba(52,211,153,0.35)', margin: '2px 0' }}>
              REPS: {repCount}/{state.settings.repetitions}
            </div>
            <div style={{ fontSize: '0.85rem', color: '#fbbf24', fontWeight: 'bold' }}>
              Score: {state.repResults.reduce((sum, r) => sum + (r.score || 0), 0)} pts
            </div>
          </div>

          {/* HUD OVERLAY 2: GÓC TRÊN PHẢI - THÔNG SỐ CẢM BIẾN REALTIME */}
          <div className="hud-panel" style={{ position: 'absolute', top: '20px', right: '20px', background: 'rgba(15, 23, 42, 0.7)', backdropFilter: 'blur(12px)', border: '1px solid rgba(255, 255, 255, 0.1)', padding: '12px 18px', borderRadius: '16px', display: 'flex', flexDirection: 'column', gap: '4px', pointerEvents: 'none', boxShadow: '0 10px 25px rgba(0,0,0,0.3)', zIndex: 5 }}>
            <div style={{ fontSize: '0.75rem', textTransform: 'uppercase', letterSpacing: '0.08em', color: '#94a3b8', fontWeight: 'bold' }}>Tín Hiệu Điện Cơ (EMG)</div>
            <div style={{ display: 'flex', alignItems: 'baseline', gap: '6px', margin: '2px 0' }}>
              <span style={{ fontSize: '1.4rem', fontWeight: '900', color: '#38bdf8', textShadow: '0 0 10px rgba(56,189,248,0.35)' }}>RMS: {Number.isFinite(state.smoothedRms) ? state.smoothedRms.toFixed(1) : '--'}</span>
              <span style={{ fontSize: '0.85rem', color: '#64748b' }}>/ {state.settings.simpleRmsThreshold}</span>
            </div>
            <div style={{ fontSize: '0.9rem', color: '#34d399', fontWeight: 'bold' }}>
              Biến thiên (STD): {emgVariation > 0 ? emgVariation.toFixed(1) : '--'}
            </div>
            <div style={{ fontSize: '0.8rem', color: '#94a3b8', marginTop: '2px' }}>
              Trạng thái: <span style={{ color: '#e2e8f0', fontWeight: 'bold' }}>{state.status}</span>
            </div>
          </div>



          {/* HUD OVERLAY 4: GÓC DƯỚI PHẢI - NÚT BẤM ĐIỀU KHIỂN GAME */}
          <div className="hud-panel" style={{ position: 'absolute', bottom: '20px', right: '20px', display: 'flex', gap: '8px', zIndex: 5 }}>
            {state.status === 'IDLE' ? (
              <button 
                type="button" 
                className="button" 
                onClick={startSession}
                style={{ background: '#10b981', color: '#fff', border: 'none', padding: '12px 24px', borderRadius: '14px', fontSize: '1rem', fontWeight: 'bold', cursor: 'pointer', boxShadow: '0 8px 16px rgba(16,185,129,0.3)', pointerEvents: 'auto' }}
              >
                ▶ BẮT ĐẦU TẬP
              </button>
            ) : (
              <>
                <button 
                  type="button" 
                  className="button-ghost" 
                  onClick={state.paused ? resumeGame : pauseGame}
                  style={{ background: 'rgba(255,255,255,0.15)', color: '#fff', border: '1px solid rgba(255,255,255,0.25)', padding: '10px 18px', borderRadius: '12px', fontWeight: 'bold', cursor: 'pointer', pointerEvents: 'auto', backdropFilter: 'blur(8px)' }}
                >
                  {state.paused ? '▶ Tiếp tục' : '⏸ Tạm dừng'}
                </button>
                <button 
                  type="button" 
                  className="button-danger" 
                  onClick={stopSession}
                  style={{ background: '#ef4444', color: '#fff', border: 'none', padding: '10px 18px', borderRadius: '12px', fontWeight: 'bold', cursor: 'pointer', boxShadow: '0 4px 12px rgba(239,68,68,0.3)', pointerEvents: 'auto' }}
                >
                  ⏹ Kết thúc
                </button>
              </>
            )}
          </div>

          {/* KẾT QUẢ CỦA REP GẦN NHẤT HÀNH TRÌNH TẬP (LƠ LỬNG TRUNG TÂM PHÍA DƯỚI) */}
          {currentRep && state.status === 'REP_RESULT' ? (
            <div style={{ position: 'absolute', bottom: '85px', left: '50%', transform: 'translateX(-50%)', zIndex: 10, minWidth: '300px', animation: 'fadeInUp 0.3s ease-out' }}>
              <RepResultCard rep={currentRep} targetActivationPct={state.settings.targetActivationPct} />
            </div>
          ) : null}
        </div>

        {/* MODAL PHỦ TOÀN MÀN HÌNH CHỈ HIỆN KHI XONG BUỔI TẬP (SESSION SUMMARY) */}
        {state.status === 'SESSION_SUMMARY' ? (
          <div className="summary-overlay" style={{ position: 'fixed', inset: 0, background: 'rgba(8, 12, 21, 0.88)', backdropFilter: 'blur(10px)', zIndex: 999, display: 'flex', alignItems: 'center', justifyContent: 'center', padding: '24px' }}>
            <div style={{ maxWidth: '600px', width: '100%', background: '#1e293b', padding: '28px', borderRadius: '28px', border: '1px solid rgba(255,255,255,0.1)', boxShadow: '0 25px 60px rgba(0,0,0,0.6)', maxHeight: '90vh', overflowY: 'auto' }}>
              <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center', marginBottom: '20px' }}>
                <h2 style={{ fontSize: '1.6rem', fontWeight: '800', color: '#fff', margin: 0 }}>Kết quả tập luyện phục hồi</h2>
                <button className="button-ghost" onClick={stopSession} style={{ padding: '6px 12px' }}>Đóng</button>
              </div>
              <SessionSummary summary={sessionSummary} repResults={state.repResults} />
              <div style={{ display: 'flex', gap: '12px', marginTop: '24px' }}>
                <button className="button" style={{ flex: 1 }} onClick={startSession}>Tập Buổi Mới</button>
                <button className="button-ghost" style={{ flex: 1 }} onClick={stopSession}>Quay Lại Trang Chủ</button>
              </div>
            </div>
          </div>
        ) : null}

        {/* OVERLAY PHỦ THÔNG BÁO MẤT KẾT NỐI REALTIME */}
        {state.status === 'CONNECTION_LOST' ? (
          <div className="connection-lost-overlay" style={{ position: 'absolute', inset: 0, background: 'rgba(239, 68, 68, 0.15)', backdropFilter: 'blur(6px)', zIndex: 8, display: 'flex', alignItems: 'center', justifyContent: 'center', borderRadius: '24px' }}>
            <div style={{ background: '#1e293b', padding: '28px', borderRadius: '20px', border: '1px solid #ef4444', textAlign: 'center', maxWidth: '380px', boxShadow: '0 20px 40px rgba(0,0,0,0.4)' }}>
              <div style={{ fontSize: '3rem', marginBottom: '12px' }}>⚠️</div>
              <h3 style={{ color: '#ef4444', fontSize: '1.3rem', fontWeight: '800', marginBottom: '8px' }}>Mất Kết Nối Cảm Biến</h3>
              <p style={{ color: '#94a3b8', fontSize: '0.9rem', marginBottom: '20px', lineHeight: '1.5' }}>
                Không nhận được dữ liệu EMG trong 4 giây gần nhất. Vui lòng kiểm tra thiết bị phần cứng hoặc kích hoạt Simulator giả lập.
              </p>
              <button className="button" onClick={restartFromConnection} style={{ width: '100%' }}>Thử Kết Nối Lại</button>
            </div>
          </div>
        ) : null}
      </main>

      <Navbar />
    </>
  );
}