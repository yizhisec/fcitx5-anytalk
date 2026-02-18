const std = @import("std");

/// Generate a v4 UUID string (36 chars: 8-4-4-4-12).
pub fn v4() [36]u8 {
    var bytes: [16]u8 = undefined;
    std.crypto.random.bytes(&bytes);

    // Set version (4) and variant (RFC 4122)
    bytes[6] = (bytes[6] & 0x0F) | 0x40;
    bytes[8] = (bytes[8] & 0x3F) | 0x80;

    var buf: [36]u8 = undefined;
    const hex = "0123456789abcdef";
    var out_i: usize = 0;
    for (0..16) |i| {
        if (i == 4 or i == 6 or i == 8 or i == 10) {
            buf[out_i] = '-';
            out_i += 1;
        }
        buf[out_i] = hex[bytes[i] >> 4];
        out_i += 1;
        buf[out_i] = hex[bytes[i] & 0x0F];
        out_i += 1;
    }
    return buf;
}
