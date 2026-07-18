DROP TABLE IF EXISTS sensor_data CASCADE;

CREATE TABLE sensor_data (
    id SERIAL PRIMARY KEY,

    device_id VARCHAR(50) NOT NULL,
    timestamp BIGINT NOT NULL,
    timestamp_ms BIGINT,

    seq INT,
    mpu_status BOOLEAN,
    battery_pct INT,
    voltage REAL,
    prediction INT,
    event VARCHAR(50),
    clock_synced BOOLEAN,
    delayed_upload BOOLEAN,

    acc_mag REAL,
    angle REAL,
    ax_g REAL,
    ay_g REAL,
    az_g REAL
);

CREATE INDEX idx_device_time 
ON sensor_data(device_id, timestamp DESC);

INSERT INTO sensor_data (
    device_id, timestamp, timestamp_ms, seq, mpu_status,
    battery_pct, voltage, prediction, event,
    clock_synced, delayed_upload,
    acc_mag, angle, ax_g, ay_g, az_g
)
VALUES (
    'health_device', 1710000005, 1710000005000, 1, true,
    85, 4.15, 0, 'Normal',
    true, false,
    1.00, 15.0, 0.0, 0.0, 1.0
);

DROP TABLE IF EXISTS emg_data CASCADE;

CREATE TABLE emg_data (
    id SERIAL PRIMARY KEY,

    device_id VARCHAR(50) NOT NULL,
    timestamp BIGINT NOT NULL,
    timestamp_ms BIGINT,

    seq INT,
    emg_status BOOLEAN,
    emg_raw_list JSONB,
    emg_rms_list JSONB
);

CREATE INDEX idx_emg_device_time
ON emg_data(device_id, timestamp DESC);

INSERT INTO emg_data (
    device_id, timestamp, timestamp_ms, seq, emg_status,
    emg_raw_list, emg_rms_list
)
VALUES (
    'health_device', 1784350000, 1784350000000, 0, true,
    '[2035,2051,2070,2062]'::jsonb,
    '[13.0,15.8,21.4,22.1]'::jsonb
);

SELECT current_database(), current_schema();

SELECT * FROM sensor_data ORDER BY id DESC LIMIT 10;