# %%
import pandas as pd
import numpy as np
from pathlib import Path
import matplotlib.pyplot as plt

NUM_OF_SENSORS = 16

BASE_DIR = Path(__file__).parent.parent   # cartella del progetto
file_path = BASE_DIR / "test" / "sensor_data_1.csv"

# --- Read CSV (no commented header anymore) ---
df = pd.read_csv(file_path)

# Detect timestamp column
possible_ts_names = ["BoardTimestamp(uint32)", "BoardTimestamp", "Timestamp"]
board_col = None

for name in possible_ts_names:
    if name in df.columns:
        board_col = name
        break

if board_col is None:
    raise ValueError("❌ Timestamp column not found in CSV.")

# --- Remove first samples if needed ---
df = df.iloc[100:].reset_index(drop=True)

# --- Create relative time axis in seconds ---
df["Time_sec"] = (df[board_col] - df[board_col].iloc[0]) / 1e6
time_axis = df["Time_sec"]

# --- Plot sensors ---
fig, ax = plt.subplots(1, 1, figsize=(12, 8))

# Sensor columns start from index 1
sensor_cols = df.columns[1:NUM_OF_SENSORS+1]

for col in sensor_cols:
    signal = df[col] - df[col].mean()  # remove DC component
    ax.plot(time_axis, signal, label=col)

ax.set_title(f"PVDF Tactile Sensor Readings (Sensors 1–{NUM_OF_SENSORS})")
ax.set_xlabel("Time (seconds)")
ax.set_ylabel("Sensor Output (V)")
ax.grid(True)
ax.legend(loc='upper right', ncol=3, fontsize=8)

plt.tight_layout()
plt.show()

# %%
