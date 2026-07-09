# %%
import pandas as pd
from pathlib import Path
import matplotlib.pyplot as plt

BASE_DIR = Path(__file__).parent.parent
csv_files = sorted(BASE_DIR.glob("test/imu_data_*.csv"))

if not csv_files:
    raise FileNotFoundError("No imu_data_*.csv file found in the test folder.")

file_path = csv_files[-1]
df = pd.read_csv(file_path)

timestamp_col = "IMUTimestamp_ms(uint32)"
if timestamp_col not in df.columns:
    raise ValueError("IMU timestamp column not found in CSV.")

df = df.reset_index(drop=True)
df["Time_sec"] = (df[timestamp_col] - df[timestamp_col].iloc[0]) / 1000.0

channels = [
    "AccelX_raw",
    "AccelY_raw",
    "AccelZ_raw",
    "GyroX_raw",
    "GyroY_raw",
    "GyroZ_raw",
]

fig, axes = plt.subplots(2, 1, figsize=(12, 8), sharex=True)

for col in channels[:3]:
    axes[0].plot(df["Time_sec"], df[col], label=col)

for col in channels[3:]:
    axes[1].plot(df["Time_sec"], df[col], label=col)

axes[0].set_title(f"IMU accelerometer raw data - {file_path.name}")
axes[0].set_ylabel("Accelerometer raw")
axes[0].grid(True)
axes[0].legend(loc="upper right")

axes[1].set_title("IMU gyroscope raw data")
axes[1].set_xlabel("Time (seconds)")
axes[1].set_ylabel("Gyroscope raw")
axes[1].grid(True)
axes[1].legend(loc="upper right")

plt.tight_layout()
plt.show()

# %%
