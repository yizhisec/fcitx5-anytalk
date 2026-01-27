use thiserror::Error;

#[allow(dead_code)]
#[derive(Debug, Error)]
pub enum DaemonError {
    #[error("Audio device error: {0}")]
    Audio(String),
    #[error("Configuration error: {0}")]
    Config(String),
    #[error("WebSocket error: {0}")]
    WebSocket(String),
    #[error("Protocol error: {0}")]
    Protocol(String),
    #[error("IO error: {0}")]
    Io(#[from] std::io::Error),
}

#[allow(dead_code)]
pub type DaemonResult<T> = Result<T, DaemonError>;
