use thiserror::Error;

#[derive(Error, Debug)]
pub enum AppError {
    #[error(transparent)]
    Api(#[from] qobuz_api::errors::QobuzApiError),
    #[error(transparent)]
    Io(#[from] std::io::Error),
    #[error("Config parse error: {0}")]
    ConfigParse(#[from] toml::de::Error),
    #[error("Config serialize error: {0}")]
    ConfigSerialize(#[from] toml::ser::Error),
    #[error("Not authenticated — run `streamer login` first")]
    NotAuthenticated,
    #[error("{0}")]
    Other(String),
}
