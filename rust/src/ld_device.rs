use hidapi::{HidApi, HidDevice};
use std::io::{self, BufRead};
use std::thread::sleep;
use std::time::Duration;

const LD_REPORT_ID: u8 = 0x00;
const LD_REPORT_SIZE: usize = 64;
const LD_MAX_PAYLOAD: usize = LD_REPORT_SIZE - 1;

pub fn run(args: &[String]) -> Result<(), String> {
    let vid = parse_u16_arg(args, "--vid")?;
    let pid = parse_u16_arg(args, "--pid")?;
    let mut payload_source = PayloadSource::from_args(args)?;
    let interval = parse_interval(args)?;
    let count = parse_count(args)?;
    let api = HidApi::new().map_err(|err| format!("Failed to initialize HID API: {err}"))?;
    let device = api
        .open(vid, pid)
        .map_err(|err| format!("Failed to open HID device {vid:#06x}:{pid:#06x}: {err}"))?;

    match count {
        Some(count) => {
            for index in 0..count {
                let payload = match payload_source.next_payload()? {
                    Some(payload) => payload,
                    None => return Ok(()),
                };
                let packet = build_packet(&payload)?;
                send_packet(&device, &packet)?;
                println!(
                    "Sent {} bytes to LD device {vid:#06x}:{pid:#06x} ({}).",
                    payload.len(),
                    index + 1
                );
                sleep(interval);
            }
        }
        None => loop {
            let payload = match payload_source.next_payload()? {
                Some(payload) => payload,
                None => return Ok(()),
            };
            let packet = build_packet(&payload)?;
            send_packet(&device, &packet)?;
            println!("Sent {} bytes to LD device {vid:#06x}:{pid:#06x}.", payload.len());
            sleep(interval);
        },
    }
    Ok(())
}

fn parse_u16_arg(args: &[String], name: &str) -> Result<u16, String> {
    let value = args
        .iter()
        .position(|arg| arg == name)
        .and_then(|idx| args.get(idx + 1))
        .ok_or_else(|| format!("Missing required argument {name}"))?;

    parse_u16_value(value)
        .map_err(|err| format!("Invalid value for {name} ({value}): {err}"))
}

enum PayloadSource {
    Raw(Vec<u8>),
    FixedMetrics {
        temperature: f32,
        power: f32,
        utilization: f32,
    },
    Stdin(io::Stdin),
}

impl PayloadSource {
    fn from_args(args: &[String]) -> Result<Self, String> {
        if let Some(raw_payload) = find_arg_value(args, "--payload") {
            return parse_payload_hex(raw_payload).map(PayloadSource::Raw);
        }

        if args.iter().any(|arg| arg == "--stdin") {
            return Ok(PayloadSource::Stdin(io::stdin()));
        }

        let temperature = parse_f32_arg(args, "--temp")?;
        let power = parse_f32_arg(args, "--power")?;
        let utilization = parse_f32_arg(args, "--util")?;

        Ok(PayloadSource::FixedMetrics {
            temperature,
            power,
            utilization,
        })
    }

    fn next_payload(&mut self) -> Result<Option<Vec<u8>>, String> {
        match self {
            PayloadSource::Raw(payload) => Ok(Some(payload.clone())),
            PayloadSource::FixedMetrics {
                temperature,
                power,
                utilization,
            } => Ok(Some(build_metrics_payload(
                *temperature,
                *power,
                *utilization,
            ))),
            PayloadSource::Stdin(stdin) => read_stdin_payload(stdin),
        }
    }
}

fn build_metrics_payload(temperature: f32, power: f32, utilization: f32) -> Vec<u8> {
    let mut payload = Vec::with_capacity(12);
    payload.extend_from_slice(&temperature.to_le_bytes());
    payload.extend_from_slice(&power.to_le_bytes());
    payload.extend_from_slice(&utilization.to_le_bytes());
    payload
}

fn parse_u16_value(value: &str) -> Result<u16, String> {
    let normalized = value.trim();
    if let Some(hex) = normalized.strip_prefix("0x") {
        u16::from_str_radix(hex, 16).map_err(|err| err.to_string())
    } else {
        normalized.parse::<u16>().map_err(|err| err.to_string())
    }
}

fn parse_f32_arg(args: &[String], name: &str) -> Result<f32, String> {
    let value = find_arg_value(args, name)
        .ok_or_else(|| format!("Missing required argument {name}"))?;

    value
        .parse::<f32>()
        .map_err(|err| format!("Invalid value for {name} ({value}): {err}"))
}

fn find_arg_value<'a>(args: &'a [String], name: &str) -> Option<&'a str> {
    args.iter()
        .position(|arg| arg == name)
        .and_then(|idx| args.get(idx + 1))
        .map(String::as_str)
}

fn parse_interval(args: &[String]) -> Result<Duration, String> {
    let value = match find_arg_value(args, "--interval-ms") {
        Some(value) => value,
        None => return Ok(Duration::from_secs(1)),
    };

    let millis = value
        .parse::<u64>()
        .map_err(|err| format!("Invalid value for --interval-ms ({value}): {err}"))?;
    if millis == 0 {
        return Err("Interval must be greater than zero.".to_string());
    }
    Ok(Duration::from_millis(millis))
}

fn parse_count(args: &[String]) -> Result<Option<usize>, String> {
    let value = match find_arg_value(args, "--count") {
        Some(value) => value,
        None => return Ok(None),
    };

    let count = value
        .parse::<usize>()
        .map_err(|err| format!("Invalid value for --count ({value}): {err}"))?;
    if count == 0 {
        return Err("Count must be greater than zero.".to_string());
    }
    Ok(Some(count))
}

fn read_stdin_payload(stdin: &io::Stdin) -> Result<Option<Vec<u8>>, String> {
    let mut line = String::new();
    let mut reader = stdin.lock();
    let bytes = reader
        .read_line(&mut line)
        .map_err(|err| format!("Failed to read stdin: {err}"))?;
    if bytes == 0 {
        return Ok(None);
    }

    let values: Vec<f32> = line
        .split(|ch: char| ch == ',' || ch.is_whitespace())
        .filter(|part| !part.is_empty())
        .map(|part| {
            part.parse::<f32>()
                .map_err(|err| format!("Invalid stdin metric ({part}): {err}"))
        })
        .collect::<Result<Vec<f32>, String>>()?;

    if values.len() != 3 {
        return Err("stdin must provide three values: temp, power, util.".to_string());
    }

    Ok(Some(build_metrics_payload(
        values[0], values[1], values[2],
    )))
}

fn parse_hex_bytes(value: &str) -> Result<Vec<u8>, String> {
    let mut bytes = Vec::new();
    let mut buffer = String::new();

    for ch in value.chars() {
        if ch.is_ascii_hexdigit() {
            buffer.push(ch);
            if buffer.len() == 2 {
                let byte =
                    u8::from_str_radix(&buffer, 16).map_err(|err| err.to_string())?;
                bytes.push(byte);
                buffer.clear();
            }
        }
    }

    if !buffer.is_empty() {
        return Err("Payload hex string must contain an even number of digits.".to_string());
    }

    Ok(bytes)
}

fn parse_payload_hex(value: &str) -> Result<Vec<u8>, String> {
    let bytes = parse_hex_bytes(value)?;
    if bytes.len() > LD_MAX_PAYLOAD {
        return Err(format!(
            "Payload is too large ({} bytes). Maximum supported is {LD_MAX_PAYLOAD}.",
            bytes.len()
        ));
    }

    if bytes.is_empty() {
        return Err("Payload cannot be empty.".to_string());
    }

    Ok(bytes)
}

fn build_packet(payload: &[u8]) -> Result<Vec<u8>, String> {
    if payload.len() > LD_MAX_PAYLOAD {
        return Err(format!(
            "Payload length {} exceeds maximum {LD_MAX_PAYLOAD}.",
            payload.len()
        ));
    }

    let mut packet = vec![0u8; LD_REPORT_SIZE + 1];
    packet[0] = LD_REPORT_ID;
    packet[1..1 + payload.len()].copy_from_slice(payload);
    Ok(packet)
}

fn send_packet(device: &HidDevice, packet: &[u8]) -> Result<(), String> {
    device
        .write(packet)
        .map_err(|err| format!("Failed to send HID report: {err}"))?;
    Ok(())
}
