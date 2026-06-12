#[derive(Debug)]
pub enum PipelineError {
    Message(String),
}

impl std::fmt::Display for PipelineError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            PipelineError::Message(s) => write!(f, "{}", s),
        }
    }
}

impl From<std::io::Error> for PipelineError {
    fn from(e: std::io::Error) -> PipelineError {
        PipelineError::Message(format!("IO Error: {}", e))
    }
}

impl From<globset::Error> for PipelineError {
    fn from(e: globset::Error) -> PipelineError {
        PipelineError::Message(format!("Glob Error: {}", e))
    }
}

impl From<csv::Error> for PipelineError {
    fn from(e: csv::Error) -> PipelineError {
        PipelineError::Message(format!("CSV Error: {}", e))
    }
}

impl From<roselib::Error> for PipelineError {
    fn from(e: roselib::Error) -> PipelineError {
        PipelineError::Message(format!("RoseLib Error: {}", e))
    }
}

impl From<std::str::Utf8Error> for PipelineError {
    fn from(e: std::str::Utf8Error) -> PipelineError {
        PipelineError::Message(format!("String Utf8 Error: {}", e))
    }
}

impl From<serde_json::Error> for PipelineError {
    fn from(e: serde_json::Error) -> PipelineError {
        PipelineError::Message(format!("JSON Error: {}", e))
    }
}