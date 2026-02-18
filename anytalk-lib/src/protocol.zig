const std = @import("std");

const PROTO_VERSION: u8 = 0b0001;
const HEADER_SIZE_4B: u8 = 0b0001;
const MSG_FULL_CLIENT_REQUEST: u8 = 0b0001;
const MSG_AUDIO_ONLY_REQUEST: u8 = 0b0010;
const MSG_FULL_SERVER_RESPONSE: u8 = 0b1001;
const MSG_ERROR_RESPONSE: u8 = 0b1111;
const FLAG_NO_SEQUENCE: u8 = 0b0000;
const FLAG_LAST_NO_SEQUENCE: u8 = 0b0010;
const SERIALIZATION_JSON: u8 = 0b0001;
const SERIALIZATION_NONE: u8 = 0b0000;
const COMPRESSION_NONE: u8 = 0b0000;

fn buildHeader(message_type: u8, flags: u8, serialization: u8, compression: u8) [4]u8 {
    return .{
        ((PROTO_VERSION & 0xF) << 4) | (HEADER_SIZE_4B & 0xF),
        ((message_type & 0xF) << 4) | (flags & 0xF),
        ((serialization & 0xF) << 4) | (compression & 0xF),
        0x00,
    };
}

fn u32be(n: usize) [4]u8 {
    return std.mem.toBytes(std.mem.nativeTo(u32, @intCast(n), .big));
}

pub fn buildFullClientRequest(allocator: std.mem.Allocator, payload_json: []const u8) ![]u8 {
    const header = buildHeader(MSG_FULL_CLIENT_REQUEST, FLAG_NO_SEQUENCE, SERIALIZATION_JSON, COMPRESSION_NONE);
    const len_bytes = u32be(payload_json.len);
    const out = try allocator.alloc(u8, 4 + 4 + payload_json.len);
    @memcpy(out[0..4], &header);
    @memcpy(out[4..8], &len_bytes);
    @memcpy(out[8..], payload_json);
    return out;
}

pub fn buildAudioOnlyRequest(allocator: std.mem.Allocator, pcm_bytes: []const u8, last: bool) ![]u8 {
    const header = buildHeader(MSG_AUDIO_ONLY_REQUEST, if (last) FLAG_LAST_NO_SEQUENCE else FLAG_NO_SEQUENCE, SERIALIZATION_NONE, COMPRESSION_NONE);
    const len_bytes = u32be(pcm_bytes.len);
    const out = try allocator.alloc(u8, 4 + 4 + pcm_bytes.len);
    @memcpy(out[0..4], &header);
    @memcpy(out[4..8], &len_bytes);
    if (pcm_bytes.len > 0) {
        @memcpy(out[8..], pcm_bytes);
    }
    return out;
}

pub const ParsedServerMessage = struct {
    kind: enum { response, @"error", unknown } = .unknown,
    flags: u8 = 0,
    json_text: ?[]const u8 = null,
    error_code: ?u32 = null,
    error_msg: ?[]const u8 = null,
};

pub fn parseServerMessage(data: []const u8) ParsedServerMessage {
    if (data.len < 4) return .{};

    const b0 = data[0];
    const b1 = data[1];
    if ((b0 >> 4) & 0xF != PROTO_VERSION or b0 & 0xF != HEADER_SIZE_4B) return .{};

    const message_type = (b1 >> 4) & 0xF;
    const flags = b1 & 0xF;

    if (data.len < 12) return .{ .flags = flags };

    if (message_type == MSG_FULL_SERVER_RESPONSE) {
        const payload_size = std.mem.readInt(u32, data[8..12], .big);
        if (data.len < 12 + payload_size) return .{ .flags = flags };
        return .{ .kind = .response, .flags = flags, .json_text = data[12 .. 12 + payload_size] };
    }

    if (message_type == MSG_ERROR_RESPONSE) {
        const code = std.mem.readInt(u32, data[4..8], .big);
        const msg_size = std.mem.readInt(u32, data[8..12], .big);
        if (data.len < 12 + msg_size) return .{ .flags = flags };
        return .{ .kind = .@"error", .flags = flags, .error_code = code, .error_msg = data[12 .. 12 + msg_size] };
    }

    return .{ .flags = flags };
}
