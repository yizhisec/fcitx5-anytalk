use std::env;
use tracing::info;

#[derive(Clone, Debug)]
pub struct AsrConfig {
    pub app_id: String,
    pub access_token: String,
    pub resource_id: String,
    pub mode: String,
}

pub fn load_config() -> Result<AsrConfig, String> {
    let app_id = env::var("ANYTALK_APP_ID")
        .map(|s| s.trim().to_string())
        .map_err(|_| "missing ANYTALK_APP_ID".to_string())?;
    let access_token = env::var("ANYTALK_ACCESS_TOKEN")
        .map(|s| s.trim().to_string())
        .map_err(|_| "missing ANYTALK_ACCESS_TOKEN".to_string())?;
    let resource_id = env::var("ANYTALK_RESOURCE_ID")
        .map(|s| s.trim().to_string())
        .unwrap_or_else(|_| "volc.seedasr.sauc.duration".to_string());
    let mode = env::var("ANYTALK_MODE")
        .map(|s| s.trim().to_string())
        .unwrap_or_else(|_| "bidi_async".to_string());

    info!(
        "Loaded Config: AppID={}, ResourceID={}, Mode={}",
        app_id, resource_id, mode
    );

    Ok(AsrConfig {
        app_id,
        access_token,
        resource_id,
        mode,
    })
}
