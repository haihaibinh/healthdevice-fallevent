DROP TABLE IF EXISTS sensor_data CASCADE;

CREATE TABLE sensor_data (
    id SERIAL PRIMARY KEY,

    device_id VARCHAR(50) NOT NULL,
    timestamp BIGINT NOT NULL,

    seq INT,
    mpu_status BOOLEAN,
    battery_pct INT,
    voltage REAL,
    prediction INT,
    event VARCHAR(50),

    acc_mag REAL,
    angle REAL,
    ax_g REAL,
    ay_g REAL,
    az_g REAL
);

CREATE INDEX idx_device_time 
ON sensor_data(device_id, timestamp DESC);

INSERT INTO sensor_data (
    device_id, timestamp, seq, mpu_status,
    battery_pct, voltage, prediction, event,
    acc_mag, angle, ax_g, ay_g, az_g
)
VALUES (
    'health_device', 1710000005, 1, true,
    85, 4.15, 0, 'Normal',
    1.00, 15.0, 0.0, 0.0, 1.0
);

SELECT current_database(), current_schema();

SELECT * FROM sensor_data ORDER BY id DESC LIMIT 10;