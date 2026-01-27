mod error;
mod protocol;
mod config;
mod audio;
mod asr;
mod ipc;

use std::env;
use std::fs;
use std::io::Result as IoResult;
use std::os::unix::fs::PermissionsExt;
use std::os::unix::net::UnixStream as StdUnixStream;
use std::path::PathBuf;
use std::sync::Arc;
use std::time::Duration;
use tokio::net::UnixListener;
use tracing::{error, info, warn};

use audio::start_global_audio;
use asr::ConnectionPool;
use config::load_config;
use ipc::handle_client;

fn socket_path() -> PathBuf {
    if let Ok(dir) = env::var("XDG_RUNTIME_DIR") {
        if !dir.trim().is_empty() {
            return PathBuf::from(dir).join("anytalk.sock");
        }
    }
    if let Ok(uid) = env::var("UID") {
        if !uid.trim().is_empty() {
            return PathBuf::from("/run/user").join(uid).join("anytalk.sock");
        }
    }
    PathBuf::from("/tmp/anytalk.sock")
}

#[tokio::main]
async fn main() -> IoResult<()> {
    // Setup Tracing (Logging to file)
    let file_appender = tracing_appender::rolling::never("/tmp", "anytalk-daemon.log");
    let (non_blocking, _guard) = tracing_appender::non_blocking(file_appender);

    tracing_subscriber::fmt()
        .with_writer(non_blocking)
        .with_env_filter(tracing_subscriber::EnvFilter::from_default_env().add_directive(tracing::Level::INFO.into()))
        .with_ansi(false) // Disable colors in file
        .init();

    info!("--------------------------------------------------------------------------------");
    info!("anytalk-daemon started. Logging to /tmp/anytalk-daemon.log");

    rustls::crypto::ring::default_provider().install_default().expect("Failed to install rustls crypto provider");

    let path = socket_path();

    let config = match load_config() {
        Ok(c) => c,
        Err(e) => {
            error!("Startup Config Error: {}", e);
            // We exit if config is missing, as we can't connect
            std::process::exit(1);
        }
    };

    let pool = Arc::new(ConnectionPool::new(config.clone()));
    let pool_for_maintainer = pool.clone();

    // Start background connection maintainer
    tokio::spawn(async move {
        pool_for_maintainer.run_maintainer().await;
    });

    // Start Persistent Audio Stream
    // We keep _stream alive here in main.
    let (_stream, audio_controller) = match start_global_audio() {
        Ok(v) => v,
        Err(e) => {
            error!("Failed to start global audio: {}", e);
            std::process::exit(1);
        }
    };

    // Take over the socket if stale. If another daemon is still alive, exit.
    if path.exists() {
        let mut stale = false;
        for attempt in 0..5 {
            match StdUnixStream::connect(&path) {
                Ok(_) => {
                    if attempt == 0 {
                        warn!("Another daemon is running; waiting briefly to see if it exits...");
                    }
                    std::thread::sleep(Duration::from_millis(200));
                }
                Err(e) => {
                    info!(
                        "Removing stale socket file {} (connect failed: {})",
                        path.display(),
                        e
                    );
                    let _ = fs::remove_file(&path);
                    stale = true;
                    break;
                }
            }
        }
        if !stale {
            error!("anytalk-daemon already running; exiting.");
            return Ok(());
        }
    }

    let listener = UnixListener::bind(&path)?;
    let _ = fs::set_permissions(&path, fs::Permissions::from_mode(0o600));
    info!("anytalk-daemon listening on {}", path.display());

    let mut sig_term = tokio::signal::unix::signal(tokio::signal::unix::SignalKind::terminate())?;

    loop {
        tokio::select! {
            res = listener.accept() => {
                match res {
                    Ok((stream, _)) => {
                        let pool_for_client = pool.clone();
                        let config_clone = config.clone();
                        let audio_for_client = audio_controller.clone();

                        tokio::spawn(async move {
                            if let Err(err) = handle_client(stream, pool_for_client, audio_for_client, config_clone).await {
                                error!("client error: {err}");
                            }
                            info!("Client handler finished.");
                        });
                    }
                    Err(e) => {
                        error!("Accept error: {}", e);
                        break;
                    }
                }
            }
            _ = tokio::signal::ctrl_c() => {
                 info!("SIGINT (Ctrl+C) received. Exiting.");
                 break;
            }
            _ = sig_term.recv() => {
                 info!("SIGTERM received. Exiting.");
                 break;
            }
        }
    }

    // Cleanup socket file
    if path.exists() {
        info!("Cleaning up socket file: {}", path.display());
        let _ = std::fs::remove_file(&path);
    }

    Ok(())
}
