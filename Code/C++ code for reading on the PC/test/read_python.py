import pandas as pd
import matplotlib.pyplot as plt
import numpy as np

file_path = "sensor_data_3.csv"

# Read CSV (skip header comment)
df = pd.read_csv(file_path, skiprows=1)

# Convert timestamp
timestamp_col = df.columns[0]
df['Time'] = pd.to_datetime(df[timestamp_col], unit='s')

# Remove first 100 samples
df = df.iloc[100:]

# Compute sample rate
samples_per_second = df.groupby(df['Time'].dt.floor('s')).size()

print("\nSamples in each second:")
for t, count in samples_per_second.items():
    print(f"{t} -> {count} samples")

average_samples_per_second = samples_per_second.mean()
print(f"\nAverage samples per second (Hz): {average_samples_per_second:.2f}")

# Time axis
df['Time_sec'] = (df['Time'] - df['Time'].iloc[0]).dt.total_seconds()
time_axis = df['Time_sec']


# ---- PLOTTING ----
fig, axs = plt.subplots(2, 1, figsize=(12, 8), sharex=True)

# First 16 sensors
for col in df.columns[1:17]:
    signal = df[col] - df[col].mean()
    axs[0].scatter(time_axis, signal, s=5, label=col)

axs[0].set_title("PVDF Tactile Sensor Readings (Sensors 1–16)")
axs[0].set_ylabel("Sensor Output (V)")
axs[0].grid(True)
axs[0].legend(loc='upper right', ncol=4, fontsize=8)

# Sensors 17–32
for col in df.columns[17:-2]:
    signal = df[col] - df[col].mean()
    axs[1].plot(time_axis, signal, label=col)

axs[1].set_title("PVDF Tactile Sensor Readings (Sensors 17–32)")
axs[1].set_xlabel("Time (seconds)")
axs[1].set_ylabel("Sensor Output (V)")
axs[1].grid(True)
axs[1].legend(loc='upper right', ncol=4, fontsize=8)

plt.tight_layout()
plt.show()
