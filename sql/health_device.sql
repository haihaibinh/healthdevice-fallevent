CREATE TABLE sensor_data (
    id SERIAL PRIMARY KEY,

    device_id VARCHAR(50) NOT NULL,
    timestamp BIGINT NOT NULL,

    battery_node1 INT,
    battery_node2 INT,

    emg INT,
    acceleration INT,
    angle INT,

    heart_rate INT,
    spo2 INT,
    hrv INT,

    risk_score FLOAT,
    event INT
);
CREATE INDEX idx_device_time 
ON sensor_data(device_id, timestamp DESC);
INSERT INTO sensor_data (
    device_id, timestamp,
    battery_node1, battery_node2,
    emg, acceleration, angle,
    heart_rate, spo2, hrv,
    risk_score, event
)
VALUES (
    'health_device', 1710000005,
    78, 66,
    120, 120, 120,
    110, 92, 20,
    0.89, 1
);
SELECT current_database(), current_schema();
ALTER TABLE sensor_data
  ALTER COLUMN acceleration TYPE REAL,
  ALTER COLUMN emg          TYPE REAL,
  ALTER COLUMN angle        TYPE REAL,
  ALTER COLUMN heart_rate   TYPE REAL,
  ALTER COLUMN spo2         TYPE REAL,
  ALTER COLUMN hrv          TYPE REAL,
  ALTER COLUMN risk_score   TYPE REAL;

 SELECT * FROM sensor_data ORDER BY id DESC LIMIT 10;