use std::fs::{create_dir_all, File, OpenOptions};
use std::io;
use std::io::Write;
use std::path::{Path, PathBuf};
use std::thread;

use crossbeam::crossbeam_channel::{Sender, Receiver, unbounded};
use chrono::Utc;
use log::{Level, Metadata, Record};
use yansi::{Color, Paint};

fn format_message(record: &CoreLoggerRecord, use_color: bool) -> String {
    let level_color = if use_color {
        match record.level {
            Level::Trace => Color::Default,
            Level::Debug => Color::Blue,
            Level::Info => Color::Green,
            Level::Warn => Color::Yellow,
            Level::Error => Color::Red,
        }
    } else {
        Color::Unset
    };

    if !record.file.is_empty() && record.line > 0 {
        format!(
            "{timestamp} [{level}] {file}({line}): {msg} \n",
            timestamp = Utc::now().format("%Y-%m-%d %H:%M:%S"),
            level = Paint::new(record.level).fg(level_color),
            file = record.file,
            line = record.line,
            msg = record.args,
        )
    } else {
        format!(
            "{timestamp} [{level}] {msg} \n",
            timestamp = Utc::now().format("%Y-%m-%d %H:%M:%S"),
            level = Paint::new(record.level).fg(level_color),
            msg = record.args
        )
    }
}

pub fn build_file_stream(path: &Path) -> io::BufWriter<File> {
    let file_stem = path
        .file_stem()
        .unwrap_or_default()
        .to_str()
        .expect(&format!(
            "No valid file stem for log file at: {}",
            path.display()
        ));

    let file_ext = path
        .extension()
        .unwrap_or_default()
        .to_str()
        .unwrap_or_default();

    let new_file_stem = format!("{}-{}", file_stem, Utc::today().format("%Y-%m-%d"));
    let mut open_path = PathBuf::from(path);
    open_path.set_file_name(new_file_stem);
    open_path.set_extension(file_ext);

    let file = OpenOptions::new()
        .append(true)
        .create(true)
        .open(&open_path)
        .expect(&format!("Could not open log file: {}", path.display()));

    io::BufWriter::new(file)
}

pub struct CoreLoggerRecord {
    level: Level,
    file: String,
    line: u32,
    args: String,
}

pub struct CoreLogger {
    sender: Sender<CoreLoggerRecord>,
}

impl CoreLogger {
    pub fn init(path: &Path) {
        create_dir_all(path.parent().unwrap())
            .expect("Failed to create intermediate dirs for logs");

        // Setup crossbeam channel
        let (s, r): (Sender<CoreLoggerRecord>, Receiver<CoreLoggerRecord>) = unbounded();

        // Create global logger instance
        let logger = Box::new(CoreLogger {
            sender: s,
        });

        log::set_boxed_logger(logger).expect("Failed to create the logger");

        // Clone path to pass to thread
        let path = PathBuf::from(path);

        // Start logging thread
        thread::spawn(move || {
            let mut file_stream = build_file_stream(&path);
            let mut file_stream_timestamp = Utc::today();

            loop {
                let res = r.recv();

                // Rotate log file stream
                if Utc::today() != file_stream_timestamp {
                    file_stream_timestamp = Utc::today();
                    file_stream = build_file_stream(&path);
                }

                if let Ok(record) = res {
                    let color_msg = format_message(&record, true);
                    let basic_msg = format_message(&record, false);

                    let _ = io::stdout().lock().write_all(color_msg.as_bytes());

                    if let Ok(()) = file_stream.write_all(basic_msg.as_bytes()) {
                        file_stream.flush().expect("Error flushing log buffer");
                    }
                } else {
                    break;
                }
            }
        });

        // Enable terminal colors on windows 10 (anniversary update)
        if cfg!(windows) && !Paint::enable_windows_ascii() {
            Paint::disable();
        }
    }
}

impl log::Log for CoreLogger {
    fn enabled(&self, metadata: &Metadata) -> bool {
        metadata.level() <= log::max_level()
    }

    fn log(&self, record: &Record) {
        if self.enabled(record.metadata()) {
            let rec = CoreLoggerRecord {
                level: record.level(),
                file: record.file().unwrap_or_default().to_string(),
                line: record.line().unwrap_or_default(),
                args: record.args().to_string(),
            };
            let _ = self.sender.send(rec);
        }
    }

    fn flush(&self) {
        // TODO: Second channel for flushing?
    }
}
