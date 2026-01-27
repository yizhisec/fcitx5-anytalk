pub const PROTO_VERSION: u8 = 0b0001;
pub const HEADER_SIZE_4B: u8 = 0b0001;
pub const MSG_FULL_CLIENT_REQUEST: u8 = 0b0001;
pub const MSG_AUDIO_ONLY_REQUEST: u8 = 0b0010;
pub const MSG_FULL_SERVER_RESPONSE: u8 = 0b1001;
pub const MSG_ERROR_RESPONSE: u8 = 0b1111;
pub const FLAG_NO_SEQUENCE: u8 = 0b0000;
pub const FLAG_LAST_NO_SEQUENCE: u8 = 0b0010;
pub const SERIALIZATION_JSON: u8 = 0b0001;
pub const SERIALIZATION_NONE: u8 = 0b0000;
pub const COMPRESSION_NONE: u8 = 0b0000;

pub fn build_header(message_type: u8, flags: u8, serialization: u8, compression: u8) -> [u8; 4] {
    let b0 = ((PROTO_VERSION & 0xF) << 4) | (HEADER_SIZE_4B & 0xF);
    let b1 = ((message_type & 0xF) << 4) | (flags & 0xF);
    let b2 = ((serialization & 0xF) << 4) | (compression & 0xF);
    [b0, b1, b2, 0x00]
}

fn u32be(n: usize) -> [u8; 4] {
    (n as u32).to_be_bytes()
}

pub fn build_full_client_request(payload_json_text: &str) -> Vec<u8> {
    let payload = payload_json_text.as_bytes();
    let mut out = Vec::with_capacity(4 + 4 + payload.len());
    let header = build_header(
        MSG_FULL_CLIENT_REQUEST,
        FLAG_NO_SEQUENCE,
        SERIALIZATION_JSON,
        COMPRESSION_NONE,
    );
    out.extend_from_slice(&header);
    out.extend_from_slice(&u32be(payload.len()));
    out.extend_from_slice(payload);
    out
}

pub fn build_audio_only_request(pcm_bytes: &[u8], last: bool) -> Vec<u8> {
    let mut out = Vec::with_capacity(4 + 4 + pcm_bytes.len());
    let header = build_header(
        MSG_AUDIO_ONLY_REQUEST,
        if last {
            FLAG_LAST_NO_SEQUENCE
        } else {
            FLAG_NO_SEQUENCE
        },
        SERIALIZATION_NONE,
        COMPRESSION_NONE,
    );
    out.extend_from_slice(&header);
    out.extend_from_slice(&u32be(pcm_bytes.len()));
    out.extend_from_slice(pcm_bytes);
    out
}

#[derive(Debug)]
pub struct ParsedServerMessage {
    pub kind: &'static str,
    pub flags: u8,
    pub json_text: Option<String>,
    pub _error_code: Option<u32>,
    pub error_msg: Option<String>,
}

pub fn parse_server_message(data: &[u8]) -> ParsedServerMessage {
    if data.len() < 4 {
        return ParsedServerMessage {
            kind: "unknown",
            flags: 0,
            json_text: None,
            _error_code: None,
            error_msg: None,
        };
    }

    let b0 = data[0];
    let b1 = data[1];
    let b2 = data[2];
    let version = (b0 >> 4) & 0xF;
    let header_size_4 = b0 & 0xF;
    if version != PROTO_VERSION || header_size_4 != HEADER_SIZE_4B {
        return ParsedServerMessage {
            kind: "unknown",
            flags: 0,
            json_text: None,
            _error_code: None,
            error_msg: None,
        };
    }

    let message_type = (b1 >> 4) & 0xF;
    let flags = b1 & 0xF;
    let _compression = b2 & 0xF;

    if message_type == MSG_FULL_SERVER_RESPONSE {
        if data.len() < 12 {
            return ParsedServerMessage {
                kind: "unknown",
                flags,
                json_text: None,
                _error_code: None,
                error_msg: None,
            };
        }
        let payload_size = u32::from_be_bytes([data[8], data[9], data[10], data[11]]) as usize;
        if data.len() < 12 + payload_size {
            return ParsedServerMessage {
                kind: "unknown",
                flags,
                json_text: None,
                _error_code: None,
                error_msg: None,
            };
        }
        let payload = &data[12..12 + payload_size];
        let json_text = String::from_utf8_lossy(payload).to_string();
        return ParsedServerMessage {
            kind: "response",
            flags,
            json_text: Some(json_text),
            _error_code: None,
            error_msg: None,
        };
    }

    if message_type == MSG_ERROR_RESPONSE {
        if data.len() < 12 {
            return ParsedServerMessage {
                kind: "unknown",
                flags,
                json_text: None,
                _error_code: None,
                error_msg: None,
            };
        }
        let code = u32::from_be_bytes([data[4], data[5], data[6], data[7]]);
        let msg_size = u32::from_be_bytes([data[8], data[9], data[10], data[11]]) as usize;
        if data.len() < 12 + msg_size {
            return ParsedServerMessage {
                kind: "unknown",
                flags,
                json_text: None,
                _error_code: None,
                error_msg: None,
            };
        }
        let msg = String::from_utf8_lossy(&data[12..12 + msg_size]).to_string();
        return ParsedServerMessage {
            kind: "error",
            flags,
            json_text: None,
            _error_code: Some(code),
            error_msg: Some(msg),
        };
    }

    ParsedServerMessage {
        kind: "unknown",
        flags,
        json_text: None,
        _error_code: None,
        error_msg: None,
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_build_header() {
        let header = build_header(
            MSG_FULL_CLIENT_REQUEST,
            FLAG_NO_SEQUENCE,
            SERIALIZATION_JSON,
            COMPRESSION_NONE,
        );
        // b0: version(0001) << 4 | header_size(0001) = 0x11
        // b1: msg_type(0001) << 4 | flags(0000) = 0x10
        // b2: serialization(0001) << 4 | compression(0000) = 0x10
        assert_eq!(header[0], 0x11);
        assert_eq!(header[1], 0x10);
        assert_eq!(header[2], 0x10);
        assert_eq!(header[3], 0x00);
    }

    #[test]
    fn test_build_full_client_request() {
        let payload = r#"{"test": "value"}"#;
        let msg = build_full_client_request(payload);

        // Should be: 4 bytes header + 4 bytes length + payload
        assert_eq!(msg.len(), 4 + 4 + payload.len());

        // Check header
        assert_eq!(msg[0], 0x11); // version + header_size
        assert_eq!(msg[1], 0x10); // msg_type + flags
        assert_eq!(msg[2], 0x10); // serialization + compression

        // Check payload length (big endian)
        let len = u32::from_be_bytes([msg[4], msg[5], msg[6], msg[7]]) as usize;
        assert_eq!(len, payload.len());

        // Check payload
        assert_eq!(&msg[8..], payload.as_bytes());
    }

    #[test]
    fn test_build_audio_only_request_not_last() {
        let audio_data = vec![0x01, 0x02, 0x03, 0x04];
        let msg = build_audio_only_request(&audio_data, false);

        assert_eq!(msg.len(), 4 + 4 + audio_data.len());
        // msg_type = AUDIO_ONLY (0010), flags = NO_SEQUENCE (0000)
        assert_eq!(msg[1] & 0xF0, 0x20); // audio only request
        assert_eq!(msg[1] & 0x0F, 0x00); // not last
    }

    #[test]
    fn test_build_audio_only_request_last() {
        let audio_data = vec![0x01, 0x02, 0x03, 0x04];
        let msg = build_audio_only_request(&audio_data, true);

        assert_eq!(msg.len(), 4 + 4 + audio_data.len());
        // flags = LAST_NO_SEQUENCE (0010)
        assert_eq!(msg[1] & 0x0F, 0x02); // is last
    }

    #[test]
    fn test_parse_server_message_too_short() {
        let short_data = vec![0x11, 0x90, 0x10];
        let result = parse_server_message(&short_data);
        assert_eq!(result.kind, "unknown");
    }

    #[test]
    fn test_parse_server_message_invalid_version() {
        // Wrong version (0010 instead of 0001)
        let data = vec![0x21, 0x90, 0x10, 0x00];
        let result = parse_server_message(&data);
        assert_eq!(result.kind, "unknown");
    }

    #[test]
    fn test_parse_server_message_response() {
        // Build a valid response message
        let json_payload = r#"{"type":"result"}"#;
        let payload_bytes = json_payload.as_bytes();

        let mut data = vec![
            0x11, // version + header_size
            0x90, // MSG_FULL_SERVER_RESPONSE (1001) << 4 | flags (0000)
            0x10, // serialization + compression
            0x00, // reserved
            0x00, 0x00, 0x00, 0x00, // sequence (4 bytes)
            0x00, 0x00, 0x00, payload_bytes.len() as u8, // payload size
        ];
        data.extend_from_slice(payload_bytes);

        let result = parse_server_message(&data);
        assert_eq!(result.kind, "response");
        assert_eq!(result.json_text, Some(json_payload.to_string()));
    }
}
