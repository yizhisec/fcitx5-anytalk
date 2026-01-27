use cpal::traits::{DeviceTrait, HostTrait, StreamTrait};
use std::sync::{Arc, Mutex};
use tokio::sync::mpsc;
use tracing::{error, info};

#[derive(Debug)]
pub enum AudioMsg {
    Chunk(Vec<u8>),
}

/// Shared controller for the global audio stream.
/// Safe to share between threads.
#[derive(Clone)]
pub struct AudioController {
    /// The current target for audio data. If None, data is dropped.
    target: Arc<Mutex<Option<mpsc::Sender<AudioMsg>>>>,
}

impl AudioController {
    fn new() -> Self {
        Self {
            target: Arc::new(Mutex::new(None)),
        }
    }

    pub fn set_target(&self, tx: mpsc::Sender<AudioMsg>) {
        let mut lock = self.target.lock().unwrap();
        *lock = Some(tx);
    }

    pub fn clear_target(&self) {
        let mut lock = self.target.lock().unwrap();
        *lock = None;
    }
}

/// Starts the global audio stream and returns the stream handle (must be kept alive) and the controller.
pub fn start_global_audio() -> Result<(cpal::Stream, AudioController), String> {
    let host = cpal::default_host();
    let device = host
        .default_input_device()
        .ok_or_else(|| "no input device".to_string())?;
    let device_name = device.name().unwrap_or_else(|_| "unknown".to_string());
    info!("Using default input device (Persistent): {}", device_name);

    let config = device
        .default_input_config()
        .map_err(|e| format!("input config error: {e}"))?;
    info!("Default input config: {:?}", config);

    let channels = config.channels() as usize;
    let in_rate = config.sample_rate().0 as usize;

    let controller = AudioController::new();
    // We only share the `target` part with the stream callback, which is thread-safe logic.
    let target_for_stream = controller.target.clone();

    let err_fn = |err| error!("audio stream error: {err}");

    let mut resampler = StreamingResampler::new(in_rate, 16000);
    // Buffer for resampling accumulation
    let mut buffer: Vec<i16> = Vec::new();
    // 200ms chunks at 16000Hz = 3200 samples
    let chunk_samples = 16000 * 200 / 1000;

    let stream = match config.sample_format() {
        cpal::SampleFormat::F32 => device.build_input_stream(
            &config.into(),
            move |data: &[f32], _| {
                process_f32(data, &mut buffer, &mut resampler, channels, chunk_samples, &target_for_stream);
            },
            err_fn,
            None,
        ),
        cpal::SampleFormat::I16 => device.build_input_stream(
            &config.into(),
            move |data: &[i16], _| {
                process_i16(data, &mut buffer, &mut resampler, channels, chunk_samples, &target_for_stream);
            },
            err_fn,
            None,
        ),
        cpal::SampleFormat::U16 => device.build_input_stream(
            &config.into(),
            move |data: &[u16], _| {
                process_u16(data, &mut buffer, &mut resampler, channels, chunk_samples, &target_for_stream);
            },
            err_fn,
            None,
        ),
        _ => return Err("unsupported sample format".to_string()),
    }.map_err(|e| format!("failed to build stream: {e}"))?;

    stream.play().map_err(|e| format!("failed to play stream: {e}"))?;
    info!("Audio stream started and running in background.");

    Ok((stream, controller))
}

// Processing helpers
fn process_f32(
    data: &[f32],
    buffer: &mut Vec<i16>,
    resampler: &mut StreamingResampler,
    channels: usize,
    chunk_samples: usize,
    target: &Mutex<Option<mpsc::Sender<AudioMsg>>>
) {
    let lock = target.lock().unwrap();
    if lock.is_none() {
        buffer.clear(); // Keep buffer clean to avoid stale audio
        return;
    }

    let mut samples: Vec<i16> = Vec::with_capacity(data.len());
    for &s in data {
        let v = (s.clamp(-1.0, 1.0) * 32767.0).round() as i16;
        samples.push(v);
    }
    push_samples(buffer, resampler, channels, &samples, chunk_samples, lock.as_ref().unwrap());
}

fn process_i16(
    data: &[i16],
    buffer: &mut Vec<i16>,
    resampler: &mut StreamingResampler,
    channels: usize,
    chunk_samples: usize,
    target: &Mutex<Option<mpsc::Sender<AudioMsg>>>
) {
    let lock = target.lock().unwrap();
    if lock.is_none() {
        buffer.clear();
        return;
    }
    push_samples(buffer, resampler, channels, data, chunk_samples, lock.as_ref().unwrap());
}

fn process_u16(
    data: &[u16],
    buffer: &mut Vec<i16>,
    resampler: &mut StreamingResampler,
    channels: usize,
    chunk_samples: usize,
    target: &Mutex<Option<mpsc::Sender<AudioMsg>>>
) {
    let lock = target.lock().unwrap();
    if lock.is_none() {
        buffer.clear();
        return;
    }
    let mut samples: Vec<i16> = Vec::with_capacity(data.len());
    for &s in data {
        samples.push(((s as i32) - 32768) as i16);
    }
    push_samples(buffer, resampler, channels, &samples, chunk_samples, lock.as_ref().unwrap());
}

struct StreamingResampler {
    in_rate: usize,
    out_rate: usize,
    pos: f64,
    tail: Vec<i16>,
}

impl StreamingResampler {
    fn new(in_rate: usize, out_rate: usize) -> Self {
        Self {
            in_rate,
            out_rate,
            pos: 0.0,
            tail: Vec::new(),
        }
    }

    fn process(&mut self, input: &[i16]) -> Vec<i16> {
        if self.in_rate == self.out_rate {
            return input.to_vec();
        }
        if input.is_empty() {
            return Vec::new();
        }
        let mut merged = Vec::with_capacity(self.tail.len() + input.len());
        merged.extend_from_slice(&self.tail);
        merged.extend_from_slice(input);

        let step = self.in_rate as f64 / self.out_rate as f64;
        let mut out = Vec::new();
        loop {
            let i0 = self.pos.floor() as usize;
            let i1 = i0 + 1;
            if i1 >= merged.len() {
                break;
            }
            let frac = self.pos - i0 as f64;
            let v0 = merged[i0] as f64;
            let v1 = merged[i1] as f64;
            let v = v0 * (1.0 - frac) + v1 * frac;
            let v = v.round().clamp(-32768.0, 32767.0) as i16;
            out.push(v);
            self.pos += step;
        }

        let base = self.pos.floor() as usize;
        let keep_from = base.saturating_sub(1);
        self.tail = merged[keep_from..].to_vec();
        self.pos -= keep_from as f64;
        out
    }
}

fn i16_to_le_bytes(samples: &[i16]) -> Vec<u8> {
    let mut out = Vec::with_capacity(samples.len() * 2);
    for s in samples {
        out.extend_from_slice(&s.to_le_bytes());
    }
    out
}

fn push_samples(
    buffer: &mut Vec<i16>,
    resampler: &mut StreamingResampler,
    channels: usize,
    input: &[i16],
    chunk_samples: usize,
    tx: &mpsc::Sender<AudioMsg>,
) {
    if input.is_empty() {
        return;
    }
    let mut mono: Vec<i16> = Vec::new();
    if channels <= 1 {
        mono.extend_from_slice(input);
    } else {
        for frame in input.chunks(channels) {
            let sum: i32 = frame.iter().map(|v| *v as i32).sum();
            let avg = (sum / frame.len() as i32) as i16;
            mono.push(avg);
        }
    }
    let resampled = resampler.process(&mono);
    buffer.extend_from_slice(&resampled);
    while buffer.len() >= chunk_samples {
        let chunk = buffer.drain(..chunk_samples).collect::<Vec<_>>();
        let bytes = i16_to_le_bytes(&chunk);
        // Use try_send to avoid blocking the audio thread if channel is full/closed
        let _ = tx.try_send(AudioMsg::Chunk(bytes));
    }
}
