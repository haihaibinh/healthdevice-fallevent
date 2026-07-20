import React, { useEffect, useRef } from 'react';

const drawRoundRectTopOnly = (ctx, x, y, width, height, radius) => {
  ctx.beginPath();
  ctx.moveTo(x, y + height); // Góc dưới bên trái
  ctx.lineTo(x, y + radius); // Đi dọc lên góc trên bên trái
  ctx.quadraticCurveTo(x, y, x + radius, y); // Bo góc trên bên trái
  ctx.lineTo(x + width - radius, y); // Đi ngang qua góc trên bên phải
  ctx.quadraticCurveTo(x + width, y, x + width, y + radius); // Bo góc trên bên phải
  ctx.lineTo(x + width, y + height); // Đi dọc xuống góc dưới bên phải
  ctx.closePath(); // Đóng đường vẽ
};

export default function SitToStandHillScene({
  rms = 0,
  threshold = 100,
  releaseThreshold = 70,
  score = 0,
  status = 'IDLE',
  instruction = '',
  onScore,
}) {
  const canvasRef = useRef(null);
  const containerRef = useRef(null);
  const onScoreRef = useRef(onScore);
  
  // Đồng bộ onScore ref
  useEffect(() => {
    onScoreRef.current = onScore;
  }, [onScore]);

  // Sử dụng ref để draw loop ở 60fps luôn truy cập dữ liệu mới nhất mà không bị stale closure
  const stateRef = useRef({
    rms,
    threshold,
    releaseThreshold,
    score,
    status,
    instruction
  });

  // Đồng bộ props vào ref
  useEffect(() => {
    stateRef.current = {
      rms,
      threshold,
      releaseThreshold,
      score,
      status,
      instruction
    };
  }, [rms, threshold, releaseThreshold, score, status, instruction]);

  useEffect(() => {
    const canvas = canvasRef.current;
    const container = containerRef.current;
    if (!canvas || !container) return;
    const ctx = canvas.getContext('2d');
    
    let animationFrameId;
    // Lấy kích thước thực tế ban đầu của container
    const initialRect = container.getBoundingClientRect();
    let width = canvas.width = Math.floor(initialRect.width) || 800;
    let height = canvas.height = Math.floor(initialRect.height) || 760;

    // Khởi tạo các biến chuyển động của game
    let ballY = height / 2;
    const ballX = 150;
    const ballRadius = 18;
    
    // Nền cuộn ngang (Parallax background offset)
    let bgOffset = 0;
    
    // Quản lý cổng (Gates) giống các ống cột trong Flappy Bird
    let gates = [];
    let lastGateSpawnTime = 0;
    const gateSpawnInterval = 420; // Số frame giữa mỗi lần sinh cổng (~7.0 giây, giãn cách cực rộng rãi)
    
    // Quản lý hạt bụi hiệu ứng (Particles)
    let particles = [];
    
    // Biến theo dõi repIndex để nổ hiệu ứng pháo hoa khi ghi nhận rep mới
    let prevRepIndex = stateRef.current.repIndex;

    // Hàm tạo hạt nổ particle
    const createExplosion = (x, y, color) => {
      for (let i = 0; i < 30; i++) {
        particles.push({
          x,
          y,
          vx: (Math.random() - 0.5) * 7,
          vy: (Math.random() - 0.5) * 7 - 1, // Hơi bay lên trên
          radius: Math.random() * 3 + 1,
          alpha: 1,
          decay: Math.random() * 0.02 + 0.015,
          color: color || '#34D399'
        });
      }
    };

    // Vòng lặp chính vẽ game 60fps
    const draw = () => {
      // Tự động đồng bộ kích thước logic của Canvas với kích thước hiển thị thực tế trên DOM để chống méo hình
      const rect = container.getBoundingClientRect();
      const currentWidth = Math.floor(rect.width) || 800;
      const currentHeight = Math.floor(rect.height) || 760;
      
      if (canvas.width !== currentWidth || canvas.height !== currentHeight) {
        canvas.width = currentWidth;
        canvas.height = currentHeight;
        width = currentWidth;
        height = currentHeight;
      }

      const { rms: curRms, threshold: curThresh, releaseThreshold: curRelease, repIndex: curRep, status: curStatus } = stateRef.current;
      
      // 1. Reset khung hình
      ctx.clearRect(0, 0, width, height);

      // Tạo gradient bầu trời đêm viễn tưởng huyền ảo (Cyberpunk Sky)
      const skyGrad = ctx.createLinearGradient(0, 0, 0, height);
      skyGrad.addColorStop(0, '#0b0f19');
      skyGrad.addColorStop(0.6, '#111827');
      skyGrad.addColorStop(1, '#1f1635'); // Chút ánh tím hồng ở chân trời
      ctx.fillStyle = skyGrad;
      ctx.fillRect(0, 0, width, height);

      // Kiểm tra game có đang hoạt động không
      const isGameRunning = curStatus === 'READY' || curStatus === 'REP_RESULT' || curStatus === 'STAND_UP';
      
      if (isGameRunning) {
        bgOffset += 1.1; // Tốc độ trôi cảnh chậm hơn
      }

      // 2. Vẽ dải sao lung linh nhấp nháy trên bầu trời
      ctx.fillStyle = 'rgba(255, 255, 255, 0.45)';
      for (let i = 0; i < 20; i++) {
        const starX = (Math.sin(i * 456.7) * 0.5 + 0.5) * width;
        const starY = (Math.cos(i * 123.4) * 0.5 + 0.5) * (height - 140);
        // Ngôi sao nhấp nháy ngẫu nhiên
        const blink = Math.sin(bgOffset * 0.05 + i) * 0.4 + 0.6;
        ctx.globalAlpha = blink;
        ctx.fillRect(starX, starY, Math.random() > 0.85 ? 2.5 : 1.2, Math.random() > 0.85 ? 2.5 : 1.2);
      }
      ctx.globalAlpha = 1.0; // Reset alpha

      // 3. Vẽ đồi núi phía xa (Parallax layer 1 - Trôi chậm)
      ctx.fillStyle = '#1e113a';
      ctx.beginPath();
      for (let x = 0; x <= width; x += 15) {
        const hillY = height - 120 + Math.sin((x + bgOffset * 0.25) * 0.003) * 50 + Math.cos((x + bgOffset * 0.15) * 0.007) * 20;
        if (x === 0) ctx.moveTo(x, hillY);
        else ctx.lineTo(x, hillY);
      }
      ctx.lineTo(width, height);
      ctx.lineTo(0, height);
      ctx.fill();

      // 4. Vẽ đồi núi phía gần (Parallax layer 2 - Trôi nhanh trung bình)
      ctx.fillStyle = '#111827';
      ctx.beginPath();
      for (let x = 0; x <= width; x += 15) {
        const hillY = height - 70 + Math.sin((x + bgOffset * 0.6) * 0.008) * 25 + Math.cos((x + bgOffset * 0.4) * 0.015) * 12;
        if (x === 0) ctx.moveTo(x, hillY);
        else ctx.lineTo(x, hillY);
      }
      ctx.lineTo(width, height);
      ctx.lineTo(0, height);
      ctx.fill();

      // Cố định vạch Threshold ở độ cao 45% của Canvas (ví dụ chiều cao 580px thì threshY khoảng 260px)
      const threshY = height * 0.45;

      // 5. Cập nhật vị trí bóng dựa trên tỷ số RMS so với Threshold (Lerp mượt mà)
      const safeThresh = curThresh > 0 ? curThresh : 100;
      const ratio = curRms / safeThresh;
      
      let targetY;
      if (ratio <= 1.0) {
        // Chưa đạt ngưỡng: Bóng di chuyển từ đáy (height - 55) lên vạch threshold (threshY)
        targetY = height - 55 - ratio * (height - 55 - threshY);
      } else {
        // Vượt ngưỡng: Bóng di chuyển từ vạch threshold (threshY) bay lên trần (40px)
        const overshoot = Math.min(ratio - 1.0, 1.0); // Khống chế độ bay cao tối đa để bóng không bay mất
        targetY = threshY - overshoot * (threshY - 40);
      }
      
      ballY += (targetY - ballY) * 0.15; // Nội suy vị trí bóng mượt mà

      // 6. Vẽ đường Ngưỡng co cơ (Threshold line - Nét đứt phát sáng vàng neon) - Đã được CỐ ĐỊNH vị trí
      ctx.save();
      ctx.strokeStyle = 'rgba(245, 158, 11, 0.7)';
      ctx.lineWidth = 2.5;
      ctx.setLineDash([8, 6]);
      ctx.shadowBlur = 10;
      ctx.shadowColor = '#F59E0B';
      ctx.beginPath();
      ctx.moveTo(0, threshY);
      ctx.lineTo(width, threshY);
      ctx.stroke();
      ctx.restore();

      ctx.fillStyle = '#fbbf24';
      ctx.font = 'bold 10px monospace';
      ctx.fillText(`NGƯỠNG CO CƠ (THRESHOLD): ${curThresh}`, 15, threshY - 6);

      // Vẽ đường Ngưỡng thả lỏng (Release line - Nét đứt màu đỏ hồng neon) - Đặt dưới vạch ngưỡng theo tỷ lệ
      const releaseRatio = Math.min(curRelease / safeThresh, 0.95);
      const releaseY = height - 55 - releaseRatio * (height - 55 - threshY);
      
      ctx.save();
      ctx.strokeStyle = 'rgba(244, 63, 94, 0.45)';
      ctx.lineWidth = 1.5;
      ctx.setLineDash([4, 4]);
      ctx.beginPath();
      ctx.moveTo(0, releaseY);
      ctx.lineTo(width, releaseY);
      ctx.stroke();
      ctx.restore();

      ctx.fillStyle = '#f43f5e';
      ctx.font = '10px monospace';
      ctx.fillText(`NGƯỠNG THẢ LỎNG (RELEASE): ${curRelease}`, 15, releaseY + 12);

      // 7. Sinh và xử lý Cổng Rehab Gates (Flappy Bird pipes) - CỐ ĐỊNH ĐỘ CAO
      if (isGameRunning) {
        lastGateSpawnTime++;
        if (lastGateSpawnTime >= gateSpawnInterval) {
          lastGateSpawnTime = 0;
          
          gates.push({
            x: width + 60,
            width: 65,
            passed: false,
            cleared: false
          });
        }
      }

      // Vẽ và di chuyển cổng - CHỈ VẼ CỘT DƯỚI CỐ ĐỊNH Ở NGƯỠNG CO CƠ (gapY = threshY)
      const gapY = threshY;

      for (let i = gates.length - 1; i >= 0; i--) {
        const gate = gates[i];
        if (isGameRunning) {
          gate.x -= 1.3; // Tốc độ trượt ngang chậm hơn giúp người tập thoải mái
        }

        const isGlowing = gate.passed && gate.cleared;
        
        ctx.save();
        ctx.shadowBlur = isGlowing ? 18 : 4;
        ctx.shadowColor = gate.passed ? (gate.cleared ? '#10B981' : '#EF4444') : '#38bdf8';
        
        // Màu cổng
        ctx.fillStyle = gate.passed 
          ? (gate.cleared ? 'rgba(16, 185, 129, 0.65)' : 'rgba(239, 68, 68, 0.25)')
          : 'rgba(56, 189, 248, 0.35)'; // Màu Neon Blue mặc định khi chưa tới
          
        ctx.strokeStyle = gate.passed
          ? (gate.cleared ? '#10B981' : '#EF4444')
          : '#38bdf8';
        ctx.lineWidth = 2.5;

        // Chỉ vẽ Cột dưới từ gapY (ngưỡng co cơ) đến sát đáy màn hình (height)
        ctx.beginPath();
        drawRoundRectTopOnly(ctx, gate.x - gate.width / 2, gapY, gate.width, height - gapY, 12);
        ctx.fill();
        ctx.stroke();
        
        ctx.restore();

        // Chữ hướng dẫn bay lơ lửng ngay trên đầu cột chưa vượt qua
        if (!gate.passed) {
          ctx.fillStyle = '#38bdf8';
          ctx.font = 'bold 9px sans-serif';
          ctx.textAlign = 'center';
          ctx.fillText("CO CƠ ĐỂ VƯỢT", gate.x, gapY - 12);
          ctx.textAlign = 'left';
        }

        // Kiểm tra va chạm với bóng tại tọa độ X
        if (!gate.passed && gate.x <= ballX) {
          gate.passed = true;
          // Quả bóng vượt qua khi bóng bay cao hơn đỉnh cột (ballY < gapY) hoặc RMS vượt ngưỡng co cơ
          const isOver = ballY < gapY || curRms >= curThresh;
          
          if (isOver) {
            gate.cleared = true;
            createExplosion(ballX, ballY, '#10B981'); // Hiệu ứng thành công xanh lá
            createExplosion(ballX, ballY - 30, '#F59E0B');
            createExplosion(ballX, ballY + 30, '#38BDF8');
            
            // Thêm chữ +1 SCORE bay lên
            particles.push({
              x: ballX + 20,
              y: ballY - 20,
              vx: 0,
              vy: -1.5,
              radius: 0,
              alpha: 1,
              decay: 0.025,
              text: '+1 SCORE!',
              color: '#34D399',
            });

            if (onScoreRef.current) {
              onScoreRef.current(1);
            }
          } else {
            gate.cleared = false;
            createExplosion(ballX, ballY, '#EF4444'); // Hiệu ứng đỏ cảnh báo
          }
        }

        // Xóa cổng ra ngoài màn hình
        if (gate.x < -80) {
          gates.splice(i, 1);
        }
      }

      // 8. Cập nhật và vẽ các hạt particle
      for (let i = particles.length - 1; i >= 0; i--) {
        const p = particles[i];
        p.x += p.vx;
        p.y += p.vy;
        p.alpha -= p.decay;
        
        if (p.alpha <= 0) {
          particles.splice(i, 1);
          continue;
        }
        
        ctx.save();
        ctx.globalAlpha = p.alpha;
        if (p.text) {
          ctx.fillStyle = p.color;
          ctx.font = 'bold 16px sans-serif';
          ctx.shadowBlur = 10;
          ctx.shadowColor = p.color;
          ctx.fillText(p.text, p.x, p.y);
        } else {
          ctx.fillStyle = p.color;
          ctx.shadowBlur = 6;
          ctx.shadowColor = p.color;
          ctx.beginPath();
          ctx.arc(p.x, p.y, p.radius, 0, Math.PI * 2);
          ctx.fill();
        }
        ctx.restore();
      }

      // 9. Vẽ quả bóng Neon phát sáng (Đại diện cho cơ đùi)
      const isCoCoActive = curRms >= curThresh;
      
      ctx.save();
      ctx.shadowBlur = isCoCoActive ? 28 : 12;
      ctx.shadowColor = isCoCoActive ? '#10B981' : '#34D399';
      
      const ballGrad = ctx.createRadialGradient(
        ballX - ballRadius * 0.25,
        ballY - ballRadius * 0.25,
        2,
        ballX,
        ballY,
        ballRadius
      );
      
      if (isCoCoActive) {
        ballGrad.addColorStop(0, '#ffffff');
        ballGrad.addColorStop(0.35, '#34d399');
        ballGrad.addColorStop(1, '#047857');
      } else {
        ballGrad.addColorStop(0, '#ffffff');
        ballGrad.addColorStop(0.35, '#a7f3d0');
        ballGrad.addColorStop(1, '#10b981');
      }
      
      ctx.fillStyle = ballGrad;
      ctx.beginPath();
      ctx.arc(ballX, ballY, ballRadius, 0, Math.PI * 2);
      ctx.fill();
      ctx.restore();

      // Vẽ đuôi lửa phát sáng nhỏ sau bóng
      if (isGameRunning && isCoCoActive) {
        ctx.fillStyle = 'rgba(52, 211, 153, 0.4)';
        ctx.beginPath();
        ctx.arc(ballX - 25, ballY + (Math.random() - 0.5) * 10, 8, 0, Math.PI * 2);
        ctx.arc(ballX - 40, ballY + (Math.random() - 0.5) * 6, 4, 0, Math.PI * 2);
        ctx.fill();
      }

      // Nhãn chữ nhỏ ở bóng
      ctx.fillStyle = isCoCoActive ? '#34d399' : '#a7f3d0';
      ctx.font = 'bold 9px sans-serif';
      ctx.textAlign = 'center';
      ctx.fillText(isCoCoActive ? "CO CƠ" : "THẢ LỎNG", ballX, ballY + ballRadius + 14);
      ctx.textAlign = 'left';

      // 10. Cột sóng phản hồi RMS bên phải Canvas
      const waveH = 100;
      const waveW = 10;
      const waveX = width - 20;
      const waveY = height - 120;
      const maxRmsForScale = 350;
      
      // Viền
      ctx.strokeStyle = 'rgba(255, 255, 255, 0.15)';
      ctx.lineWidth = 1;
      ctx.strokeRect(waveX, waveY, waveW, waveH);
      
      // Điền
      const fillPercent = Math.min(curRms / maxRmsForScale, 1);
      ctx.fillStyle = isCoCoActive ? '#10B981' : '#34D399';
      ctx.fillRect(waveX, waveY + waveH * (1 - fillPercent), waveW, waveH * fillPercent);

      // 11. Hiển thị bảng số SCORE trực tiếp trên Canvas
      const curScore = stateRef.current.score || 0;
      ctx.save();
      ctx.fillStyle = 'rgba(15, 23, 42, 0.75)';
      ctx.strokeStyle = 'rgba(52, 211, 153, 0.5)';
      ctx.lineWidth = 2;
      drawRoundRectTopOnly(ctx, width / 2 - 90, 15, 180, 44, 14);
      ctx.fill();
      ctx.stroke();

      ctx.fillStyle = '#34D399';
      ctx.font = '900 18px sans-serif';
      ctx.textAlign = 'center';
      ctx.shadowBlur = 10;
      ctx.shadowColor = '#34D399';
      ctx.fillText(`SCORE: ${curScore}`, width / 2, 43);
      ctx.restore();

      // Hiển thị Banner khi vừa ghi nhận Rep thành công
      if (curStatus === 'REP_RESULT') {
        ctx.save();
        ctx.fillStyle = 'rgba(16, 185, 129, 0.88)';
        ctx.shadowBlur = 15;
        ctx.shadowColor = '#10B981';
        ctx.beginPath();
        drawRoundRectTopOnly(ctx, width / 2 - 140, 68, 280, 36, 10);
        ctx.fill();

        ctx.fillStyle = '#ffffff';
        ctx.font = 'bold 14px sans-serif';
        ctx.textAlign = 'center';
        ctx.fillText(`🎉 CHÚC MỪNG! HOÀN THÀNH REP ${curRep}`, width / 2, 91);
        ctx.restore();
      }

      // Hiển thị chữ PAUSED lớn phủ toàn màn hình khi dừng game
      if (curStatus === 'PAUSED') {
        ctx.fillStyle = 'rgba(15, 23, 42, 0.78)';
        ctx.fillRect(0, 0, width, height);
        
        ctx.fillStyle = '#ffffff';
        ctx.font = 'bold 24px sans-serif';
        ctx.textAlign = 'center';
        ctx.fillText("BÀI TẬP ĐANG TẠM DỪNG", width / 2, height / 2 - 10);
        ctx.font = '13px sans-serif';
        ctx.fillStyle = '#94a3b8';
        ctx.fillText("Nhấn 'Tiếp tục' ở góc dưới phải để luyện tập lại", width / 2, height / 2 + 20);
        ctx.textAlign = 'left';
      }

      animationFrameId = requestAnimationFrame(draw);
    };

    draw();

    return () => {
      cancelAnimationFrame(animationFrameId);
    };
  }, []);

  return (
    <div className="game-shell full-width-game" ref={containerRef} style={{ position: 'relative', width: '100%', minHeight: '760px', overflow: 'hidden' }}>
      <canvas ref={canvasRef} style={{ display: 'block', width: '100%', height: '760px' }} />
    </div>
  );
}