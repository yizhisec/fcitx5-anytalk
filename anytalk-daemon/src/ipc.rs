use crate::asr::{connect_to_asr, run_session, ConnectionPool};
use crate::audio::AudioController;
use crate::config::AsrConfig;
use serde::{Deserialize, Serialize};
use std::io::Result as IoResult;
use std::sync::Arc;
use tokio::io::{AsyncBufReadExt, AsyncWriteExt, BufReader};
use tokio::net::UnixStream;
use tokio::sync::mpsc;
use tracing::{debug, error, info, warn};

#[derive(Debug, Deserialize)]
#[serde(tag = "type")]
enum ClientMsg {
    #[serde(rename = "start")]
    Start { _mode: Option<String> },
    #[serde(rename = "stop")]
    Stop,
    #[serde(rename = "cancel")]
    Cancel,
    #[serde(other)]
    Other,
}

#[derive(Debug, Serialize)]
#[serde(tag = "type")]
pub enum ServerMsg<'a> {
    #[serde(rename = "status")]
    Status { state: &'a str },
    #[serde(rename = "partial")]
    Partial { text: &'a str },
    #[serde(rename = "final")]
    Final { text: &'a str },
    #[serde(rename = "error")]
    Error { message: &'a str },
}

pub fn serialize_msg(msg: ServerMsg<'_>) -> String {
    let mut line = serde_json::to_string(&msg).unwrap_or_else(|_| "{}".to_string());
    line.push('\n');
    line
}

pub async fn handle_client(stream: UnixStream, pool: Arc<ConnectionPool>, audio_ctrl: AudioController, cfg: AsrConfig) -> IoResult<()> {
    let (read_half, mut write_half) = stream.into_split();
    let mut reader = BufReader::new(read_half).lines();

    let (resp_tx, mut resp_rx) = mpsc::channel::<String>(32);

    // Active session: (WebSocketTask)
    let mut session: Option<tokio::task::JoinHandle<()>> = None;
    // Task from a previous session that was stopped but is still finishing up (processing final results)
    let mut draining_task: Option<tokio::task::JoinHandle<()>> = None;

    info!("New client connected to daemon");

    // Immediately inform client if we are ready
    {
        let lock = pool.spare.lock().await;
        if lock.is_some() {
             let _ = write_half.write_all(serialize_msg(ServerMsg::Status { state: "connected" }).as_bytes()).await;
        }
    }

    loop {
        tokio::select! {
            line = reader.next_line() => {
                let line = match line? {
                    Some(l) => l,
                    None => {
                        info!("Client disconnected");
                        break;
                    }
                };
                debug!("Received: {}", line);
                let msg: ClientMsg = serde_json::from_str(&line).unwrap_or(ClientMsg::Other);
                match msg {
                    ClientMsg::Start { .. } => {
                        info!("Received Start command");

                        // 1. If there's a draining task (previous session finishing up), abort it.
                        if let Some(task) = draining_task.take() {
                            info!("Aborting draining task from previous session");
                            task.abort();
                        }

                        // 2. If there's an active session, abort it too (force restart).
                        if let Some(task) = session.take() {
                            warn!("Aborting active session for new Start");
                            // IMPORTANT: Stop audio flow first!
                            audio_ctrl.clear_target();
                            task.abort();
                        }

                        // Try to get hot spare
                        let maybe_ws = pool.take().await;
                        let ws_stream = match maybe_ws {
                            Some(s) => {
                                info!("Using hot spare connection");
                                s
                            },
                            None => {
                                info!("No hot spare, connecting on demand...");
                                let _ = write_half.write_all(serialize_msg(ServerMsg::Status { state: "connecting" }).as_bytes()).await;
                                match connect_to_asr(&cfg).await {
                                    Ok(s) => s,
                                    Err(e) => {
                                        error!("Connection failed: {}", e);
                                        let _ = write_half.write_all(serialize_msg(ServerMsg::Error { message: &e }).as_bytes()).await;
                                        continue;
                                    }
                                }
                            }
                        };

                        // Prepare Audio Channel
                        let (audio_tx, audio_rx) = mpsc::channel(16);

                        // Route global audio to this channel
                        audio_ctrl.set_target(audio_tx);

                        let resp_tx_clone = resp_tx.clone();
                        let cfg_clone = cfg.clone();
                        let ws_task = tokio::spawn(async move {
                            if let Err(e) = run_session(ws_stream, audio_rx, resp_tx_clone.clone(), cfg_clone).await {
                                error!("run_session error: {}", e);
                                let _ = resp_tx_clone
                                    .send(serialize_msg(ServerMsg::Error { message: &e }))
                                    .await;
                            }
                            // Session done (successfully or error)
                            let _ = resp_tx_clone
                                .send(serialize_msg(ServerMsg::Status { state: "idle" }))
                                .await;
                        });
                        session = Some(ws_task);
                        let _ = write_half.write_all(serialize_msg(ServerMsg::Status { state: "recording" }).as_bytes()).await;
                    }
                    ClientMsg::Stop => {
                        info!("Received Stop command");
                        // Stop audio flow immediately
                        audio_ctrl.clear_target();

                        if let Some(ws_task) = session.take() {
                            // Move WS task to draining.
                            if let Some(old_draining) = draining_task.replace(ws_task) {
                                old_draining.abort();
                            }
                        } else {
                            let _ = write_half.write_all(serialize_msg(ServerMsg::Status { state: "idle" }).as_bytes()).await;
                        }
                    }
                    ClientMsg::Cancel => {
                        info!("Received Cancel command");
                        audio_ctrl.clear_target();
                        if let Some(ws_task) = session.take() {
                            ws_task.abort();
                        }
                        if let Some(task) = draining_task.take() {
                            task.abort();
                        }
                        let _ = write_half.write_all(serialize_msg(ServerMsg::Status { state: "idle" }).as_bytes()).await;
                    }
                    ClientMsg::Other => {
                        warn!("Received unknown message");
                        let _ = write_half.write_all(serialize_msg(ServerMsg::Error { message: "unknown message" }).as_bytes()).await;
                    }
                }
            }
            resp = resp_rx.recv() => {
                if let Some(line) = resp {
                    debug!("Sending to client: {}", line.trim());
                    let _ = write_half.write_all(line.as_bytes()).await;
                }
            }
        }
    }

    // Ensure audio stops if client drops unexpectedly
    audio_ctrl.clear_target();

    Ok(())
}
