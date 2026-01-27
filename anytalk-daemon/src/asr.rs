use crate::audio::AudioMsg;
use crate::config::AsrConfig;
use crate::ipc::{serialize_msg, ServerMsg};
use crate::protocol::{build_audio_only_request, build_full_client_request, parse_server_message};
use futures_util::{SinkExt, StreamExt};
use std::sync::Arc;
use std::time::Duration;
use tokio::sync::{mpsc, Mutex as TokioMutex, Notify};
use tokio::time::sleep;
use tokio_tungstenite::tungstenite::client::IntoClientRequest;
use tokio_tungstenite::tungstenite::protocol::Message;
use tokio_tungstenite::{MaybeTlsStream, WebSocketStream};
use tracing::{debug, error, info, warn};

pub type WsStream = WebSocketStream<MaybeTlsStream<tokio::net::TcpStream>>;

/// Manages a single "hot spare" connection.
pub struct ConnectionPool {
    /// The pre-connected stream.
    pub spare: Arc<TokioMutex<Option<WsStream>>>,
    /// Notify when the spare is consumed, so the background task can reconnect.
    notify_consumed: Arc<Notify>,
    /// Config to use for connecting.
    config: AsrConfig,
}

impl ConnectionPool {
    pub fn new(config: AsrConfig) -> Self {
        Self {
            spare: Arc::new(TokioMutex::new(None)),
            notify_consumed: Arc::new(Notify::new()),
            config,
        }
    }

    /// Takes the spare connection if available.
    pub async fn take(&self) -> Option<WsStream> {
        let mut lock = self.spare.lock().await;
        let stream = lock.take();
        if stream.is_some() {
            self.notify_consumed.notify_one();
        }
        stream
    }

    /// Background task to maintain the spare connection.
    pub async fn run_maintainer(self: Arc<Self>) {
        loop {
            // Check if we need a connection
            let needs_conn = {
                let lock = self.spare.lock().await;
                lock.is_none()
            };

            if needs_conn {
                info!("Pre-connecting to Doubao...");
                match connect_to_asr(&self.config).await {
                    Ok(stream) => {
                        info!("Pre-connection established. Ready.");
                        let mut lock = self.spare.lock().await;
                        *lock = Some(stream);
                    }
                    Err(e) => {
                        error!("Pre-connection failed: {}. Retrying in 3s...", e);
                        sleep(Duration::from_secs(3)).await;
                        continue;
                    }
                }
            }

            // Wait until consumed
            self.notify_consumed.notified().await;
            // Slight delay to avoid hammering if something is spiraling,
            // but short enough to be ready for the next phrase.
            sleep(Duration::from_millis(100)).await;
        }
    }
}

pub async fn connect_to_asr(cfg: &AsrConfig) -> Result<WsStream, String> {
    let url = asr_url(&cfg.mode);
    debug!("Dialing ASR: {}", url);
    let mut request = url
        .into_client_request()
        .map_err(|e| format!("ws request error: {e}"))?;
    {
        let headers = request.headers_mut();
        headers.insert(
            "X-Api-App-Key",
            cfg.app_id.parse().map_err(|_| "bad app id")?,
        );
        headers.insert(
            "X-Api-Access-Key",
            cfg.access_token.parse().map_err(|_| "bad access token")?,
        );
        headers.insert(
            "X-Api-Resource-Id",
            cfg.resource_id.parse().map_err(|_| "bad resource id")?,
        );
        headers.insert(
            "X-Api-Connect-Id",
            uuid::Uuid::new_v4()
                .to_string()
                .parse()
                .map_err(|_| "bad uuid")?,
        );
    }

    let (ws_stream, _) = tokio_tungstenite::connect_async(request)
        .await
        .map_err(|e| format!("ws connect error: {e}"))?;
    Ok(ws_stream)
}

fn asr_url(mode: &str) -> &'static str {
    match mode {
        "bidi" => "wss://openspeech.bytedance.com/api/v3/sauc/bigmodel",
        "bidi_async" => "wss://openspeech.bytedance.com/api/v3/sauc/bigmodel_async",
        _ => "wss://openspeech.bytedance.com/api/v3/sauc/bigmodel_nostream",
    }
}

fn default_request_json(mode: &str) -> String {
    let is_nostream = mode == "nostream";
    let mut obj = serde_json::json!({
        "user": {"uid": "anytalk"},
        "audio": {
            "format": "pcm",
            "rate": 16000,
            "bits": 16,
            "channel": 1
        },
        "request": {
            "model_name": "bigmodel",
            "enable_itn": true,
            "enable_punc": true,
            "enable_ddc": false,
            "enable_word": false,
            "res_type": "full",
            "nbest": 1,
            "use_vad": true
        }
    });
    if is_nostream {
        if let Some(audio) = obj.get_mut("audio") {
            audio["language"] = serde_json::Value::String("zh-CN".to_string());
        }
    }
    obj.to_string()
}

pub async fn run_session(
    ws_stream: WsStream,
    mut audio_rx: mpsc::Receiver<AudioMsg>,
    resp_tx: mpsc::Sender<String>,
    cfg: AsrConfig,
) -> Result<(), String> {
    info!("Starting session on existing WS connection");
    let (mut ws_write, mut ws_read) = ws_stream.split();

    let req_json = default_request_json(&cfg.mode);
    debug!("Sending initial request: {}", req_json);
    let frame = build_full_client_request(&req_json);
    ws_write
        .send(Message::Binary(frame))
        .await
        .map_err(|e| format!("ws send error: {e}"))?;

    let mut last_committed_end_time: i64 = -1;
    let mut last_full_text = String::new();
    let mut chunk_count = 0;
    let mut audio_active = true;

    loop {
        tokio::select! {
            audio = audio_rx.recv(), if audio_active => {
                match audio {
                    Some(AudioMsg::Chunk(bytes)) => {
                        chunk_count += 1;
                        if chunk_count % 20 == 0 {
                            debug!("Sent 20 audio chunks to ASR...");
                        }
                        let frame = build_audio_only_request(&bytes, false);
                        if ws_write.send(Message::Binary(frame)).await.is_err() {
                            audio_active = false;
                        }
                    }
                    None => {
                        debug!("Audio source channel closed (Stop received)");
                        // IMPORTANT: Send an empty chunk with last=true to tell ASR we are done.
                        let empty = Vec::new();
                        let frame = build_audio_only_request(&empty, true);
                        if let Err(e) = ws_write.send(Message::Binary(frame)).await {
                             warn!("Failed to send final frame: {}", e);
                        }
                        audio_active = false;
                    }
                }
            }
            msg = ws_read.next() => {
                match msg {
                    Some(Ok(Message::Binary(data))) => {
                        let parsed = parse_server_message(&data);
                        if parsed.kind == "error" {
                            let msg = parsed.error_msg.unwrap_or_else(|| "server error".to_string());
                            error!("ASR Error: {}", msg);
                            let _ = resp_tx.send(serialize_msg(ServerMsg::Error { message: &msg })).await;
                            break;
                        }
                        if parsed.kind != "response" {
                            continue;
                        }
                        if let Some(json_text) = parsed.json_text {
                            debug!("ASR Response (flags={:b}): {}", parsed.flags, json_text);
                            let (partial, finals) = parse_asr_texts(&json_text, &mut last_committed_end_time, &mut last_full_text, cfg.mode.as_str());
                            if let Some(p) = partial {
                                let _ = resp_tx.send(serialize_msg(ServerMsg::Partial { text: &p })).await;
                            }
                            for f in finals {
                                debug!("Committing final text: {}", f);
                                let _ = resp_tx.send(serialize_msg(ServerMsg::Final { text: &f })).await;
                            }
                            // 0b0011 means this is the final response frame from server
                            if parsed.flags == 0b0011 {
                                info!("Received final server response frame. Closing.");
                                break;
                            }
                        }
                    }
                    Some(Ok(Message::Close(_))) => {
                        info!("WebSocket closed by server");
                        break;
                    }
                    Some(Ok(_)) => {},
                    Some(Err(e)) => {
                        error!("WebSocket error: {}", e);
                        break;
                    }
                    None => {
                        debug!("WebSocket stream ended (None)");
                        break;
                    }
                }
            }
        }
    }

    Ok(())
}

fn parse_asr_texts(
    json_text: &str,
    last_committed_end_time: &mut i64,
    last_full_text: &mut String,
    mode: &str,
) -> (Option<String>, Vec<String>) {
    let mut partial: Option<String> = None;
    let mut finals: Vec<String> = Vec::new();

    let obj: serde_json::Value = match serde_json::from_str(json_text) {
        Ok(v) => v,
        Err(_) => return (None, finals),
    };
    let result = match obj.get("result") {
        Some(r) => r,
        None => return (None, finals),
    };

    if let Some(utterances) = result.get("utterances").and_then(|u| u.as_array()) {
        for u in utterances {
            let def = u.get("definite").and_then(|v| v.as_bool()).unwrap_or(false);
            if !def {
                continue;
            }
            let end_time = u.get("end_time").and_then(|v| v.as_i64()).unwrap_or(-1);
            if end_time <= *last_committed_end_time {
                debug!(
                    "Skipping definite utterance: end_time {} <= last {}",
                    end_time,
                    last_committed_end_time
                );
                continue;
            }
            if let Some(txt) = u.get("text").and_then(|v| v.as_str()) {
                let trimmed = txt.trim();
                if !trimmed.is_empty() {
                    debug!("New final: {} (end_time {})", trimmed, end_time);
                    finals.push(trimmed.to_string());
                    *last_committed_end_time = end_time;
                }
            }
        }
        for u in utterances.iter().rev() {
            if u.get("definite").and_then(|v| v.as_bool()).unwrap_or(false) {
                continue;
            }
            if let Some(txt) = u.get("text").and_then(|v| v.as_str()) {
                let trimmed = txt.trim();
                if !trimmed.is_empty() {
                    partial = Some(trimmed.to_string());
                    break;
                }
            }
        }
        return (partial, finals);
    }

    if let Some(txt) = result.get("text").and_then(|v| v.as_str()) {
        let full = txt.trim().to_string();
        if full.is_empty() {
            return (None, finals);
        }
        if mode == "bidi_async" {
            partial = Some(full.clone());
            finals.push(full.clone());
        } else if !last_full_text.is_empty() && full.starts_with(last_full_text.as_str()) {
            let suffix = full[last_full_text.len()..].trim();
            if !suffix.is_empty() {
                finals.push(suffix.to_string());
            }
        } else if full != *last_full_text {
            finals.push(full.clone());
        }
        *last_full_text = full;
    }

    (partial, finals)
}

