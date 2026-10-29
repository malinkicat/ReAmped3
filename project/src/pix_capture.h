// pix_capture.h - Debug: GPU captures with PIX from inside the game

#pragma once

namespace amped3::pix {

// Loads WinPixGpuCapturer.dll from the newest PIX install (before the D3D12
// device is created).
void LoadCapturer();

// Captures the next `frames` frames to private_work\gpu_<time>.wpix.
void CaptureNextFrames(int frames);

}  // namespace amped3::pix
