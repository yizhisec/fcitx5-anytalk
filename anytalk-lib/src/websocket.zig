const std = @import("std");
const TlsStream = @import("tls.zig").TlsStream;
const log = std.log.scoped(.websocket);

pub const Opcode = enum(u4) {
    continuation = 0,
    text = 1,
    binary = 2,
    close = 8,
    ping = 9,
    pong = 10,
    _,
};

pub const WsFrame = struct {
    opcode: Opcode,
    payload: []const u8,
    fin: bool,
};

pub const WebSocket = struct {
    tls: TlsStream,
    allocator: std.mem.Allocator,
    read_buf: []u8,
    read_pos: usize,
    read_len: usize,

    const READ_BUF_SIZE = 65536;

    pub fn connect(
        allocator: std.mem.Allocator,
        host: []const u8,
        port: u16,
        path: []const u8,
        headers: []const [2][]const u8,
    ) !WebSocket {
        var tls_stream = try TlsStream.connect(host, port);
        errdefer tls_stream.close();

        // Build HTTP upgrade request
        var req_buf: std.ArrayList(u8) = .{};
        defer req_buf.deinit(allocator);
        const w = req_buf.writer(allocator);

        // Generate WebSocket key
        var key_bytes: [16]u8 = undefined;
        std.crypto.random.bytes(&key_bytes);
        var key_buf: [24]u8 = undefined;
        _ = std.base64.standard.Encoder.encode(&key_buf, &key_bytes);
        const key_len = std.base64.standard.Encoder.calcSize(16);
        const ws_key_str = key_buf[0..key_len];

        try w.print("GET {s} HTTP/1.1\r\n", .{path});
        try w.print("Host: {s}\r\n", .{host});
        try w.writeAll("Upgrade: websocket\r\n");
        try w.writeAll("Connection: Upgrade\r\n");
        try w.print("Sec-WebSocket-Key: {s}\r\n", .{ws_key_str});
        try w.writeAll("Sec-WebSocket-Version: 13\r\n");
        for (headers) |hdr| {
            try w.print("{s}: {s}\r\n", .{ hdr[0], hdr[1] });
        }
        try w.writeAll("\r\n");

        try tls_stream.write(req_buf.items);

        // Read HTTP response (just check for 101)
        var resp_buf: [4096]u8 = undefined;
        var resp_len: usize = 0;
        while (resp_len < resp_buf.len) {
            const n = try tls_stream.read(resp_buf[resp_len..]);
            resp_len += n;
            // Check if we have the full headers
            if (std.mem.indexOf(u8, resp_buf[0..resp_len], "\r\n\r\n") != null) break;
        }

        // Verify 101 status
        const resp_str = resp_buf[0..resp_len];
        if (!std.mem.startsWith(u8, resp_str, "HTTP/1.1 101")) {
            log.err("WebSocket handshake failed: {s}", .{resp_str[0..@min(resp_len, 80)]});
            return error.HandshakeFailed;
        }

        const read_buf = try allocator.alloc(u8, READ_BUF_SIZE);

        return WebSocket{
            .tls = tls_stream,
            .allocator = allocator,
            .read_buf = read_buf,
            .read_pos = 0,
            .read_len = 0,
        };
    }

    fn fillBuf(self: *WebSocket) !void {
        if (self.read_pos >= self.read_len) {
            self.read_pos = 0;
            self.read_len = try self.tls.read(self.read_buf);
        }
    }

    fn readByte(self: *WebSocket) !u8 {
        try self.fillBuf();
        if (self.read_pos >= self.read_len) return error.ConnectionClosed;
        const b = self.read_buf[self.read_pos];
        self.read_pos += 1;
        return b;
    }

    fn readBytes(self: *WebSocket, buf: []u8) !void {
        var offset: usize = 0;
        while (offset < buf.len) {
            try self.fillBuf();
            if (self.read_pos >= self.read_len) return error.ConnectionClosed;
            const available = self.read_len - self.read_pos;
            const need = buf.len - offset;
            const copy_len = @min(available, need);
            @memcpy(buf[offset .. offset + copy_len], self.read_buf[self.read_pos .. self.read_pos + copy_len]);
            self.read_pos += copy_len;
            offset += copy_len;
        }
    }

    pub fn readFrame(self: *WebSocket) !WsFrame {
        const b0 = try self.readByte();
        const b1 = try self.readByte();

        const fin = (b0 & 0x80) != 0;
        const opcode: Opcode = @enumFromInt(@as(u4, @truncate(b0 & 0x0F)));
        const masked = (b1 & 0x80) != 0;
        var payload_len: u64 = b1 & 0x7F;

        if (payload_len == 126) {
            var len_buf: [2]u8 = undefined;
            try self.readBytes(&len_buf);
            payload_len = std.mem.readInt(u16, &len_buf, .big);
        } else if (payload_len == 127) {
            var len_buf: [8]u8 = undefined;
            try self.readBytes(&len_buf);
            payload_len = std.mem.readInt(u64, &len_buf, .big);
        }

        if (payload_len > 16 * 1024 * 1024) return error.FrameTooLarge; // 16MB limit

        var mask_key: [4]u8 = undefined;
        if (masked) {
            try self.readBytes(&mask_key);
        }

        const payload = try self.allocator.alloc(u8, @intCast(payload_len));
        errdefer self.allocator.free(payload);
        if (payload_len > 0) {
            try self.readBytes(payload);
            if (masked) {
                for (payload, 0..) |*b, i| {
                    b.* ^= mask_key[i % 4];
                }
            }
        }

        return WsFrame{
            .opcode = opcode,
            .payload = payload,
            .fin = fin,
        };
    }

    pub fn sendBinary(self: *WebSocket, data: []const u8) !void {
        try self.sendFrame(@intFromEnum(Opcode.binary), data);
    }

    pub fn sendClose(self: *WebSocket) void {
        self.sendFrame(@intFromEnum(Opcode.close), &.{}) catch {};
    }

    pub fn sendPong(self: *WebSocket, data: []const u8) !void {
        try self.sendFrame(@intFromEnum(Opcode.pong), data);
    }

    fn sendFrame(self: *WebSocket, opcode: u8, data: []const u8) !void {
        // Client frames must be masked
        var mask_key: [4]u8 = undefined;
        std.crypto.random.bytes(&mask_key);

        var header_buf: [14]u8 = undefined;
        var header_len: usize = 2;

        header_buf[0] = 0x80 | (opcode & 0x0F); // FIN + opcode
        if (data.len < 126) {
            header_buf[1] = 0x80 | @as(u8, @intCast(data.len));
        } else if (data.len < 65536) {
            header_buf[1] = 0x80 | 126;
            std.mem.writeInt(u16, header_buf[2..4], @intCast(data.len), .big);
            header_len = 4;
        } else {
            header_buf[1] = 0x80 | 127;
            std.mem.writeInt(u64, header_buf[2..10], @intCast(data.len), .big);
            header_len = 10;
        }

        @memcpy(header_buf[header_len .. header_len + 4], &mask_key);
        header_len += 4;

        try self.tls.write(header_buf[0..header_len]);

        if (data.len > 0) {
            // Apply mask to data before sending
            const masked = try self.allocator.alloc(u8, data.len);
            defer self.allocator.free(masked);
            for (data, 0..) |b, i| {
                masked[i] = b ^ mask_key[i % 4];
            }
            try self.tls.write(masked);
        }
    }

    pub fn close(self: *WebSocket) void {
        self.sendClose();
        self.tls.close();
        self.allocator.free(self.read_buf);
    }
};
