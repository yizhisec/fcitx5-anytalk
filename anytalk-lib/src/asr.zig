const std = @import("std");
const WebSocket = @import("websocket.zig").WebSocket;
const protocol = @import("protocol.zig");
const json_mod = @import("json.zig");
const uuid_mod = @import("uuid.zig");
const audio_mod = @import("audio.zig");
const log = std.log.scoped(.asr);

pub const AsrConfig = struct {
    app_id: []const u8,
    access_token: []const u8,
    resource_id: []const u8,
    mode: []const u8,
};

fn asrUrl(mode: []const u8) struct { host: []const u8, path: []const u8 } {
    if (std.mem.eql(u8, mode, "bidi")) {
        return .{ .host = "openspeech.bytedance.com", .path = "/api/v3/sauc/bigmodel" };
    } else if (std.mem.eql(u8, mode, "bidi_async")) {
        return .{ .host = "openspeech.bytedance.com", .path = "/api/v3/sauc/bigmodel_async" };
    } else {
        return .{ .host = "openspeech.bytedance.com", .path = "/api/v3/sauc/bigmodel_nostream" };
    }
}

pub fn connectToAsr(allocator: std.mem.Allocator, cfg: *const AsrConfig) !WebSocket {
    const url = asrUrl(cfg.mode);
    const connect_id = uuid_mod.v4();

    const headers = [_][2][]const u8{
        .{ "X-Api-App-Key", cfg.app_id },
        .{ "X-Api-Access-Key", cfg.access_token },
        .{ "X-Api-Resource-Id", cfg.resource_id },
        .{ "X-Api-Connect-Id", &connect_id },
    };

    log.info("Connecting to ASR: {s}{s}", .{ url.host, url.path });
    return WebSocket.connect(allocator, url.host, 443, url.path, &headers);
}

/// Hot spare connection pool - maintains one pre-connected WebSocket.
pub const ConnectionPool = struct {
    spare: ?WebSocket,
    mutex: std.Thread.Mutex,
    allocator: std.mem.Allocator,
    cfg: AsrConfig,
    thread: ?std.Thread,
    running: std.atomic.Value(bool),
    consumed: std.Thread.ResetEvent,

    pub fn init(allocator: std.mem.Allocator, cfg: AsrConfig) ConnectionPool {
        return ConnectionPool{
            .spare = null,
            .mutex = .{},
            .allocator = allocator,
            .cfg = cfg,
            .thread = null,
            .running = std.atomic.Value(bool).init(false),
            .consumed = .{},
        };
    }

    pub fn start(self: *ConnectionPool) !void {
        if (self.running.load(.acquire)) return;
        self.running.store(true, .release);
        self.thread = try std.Thread.spawn(.{}, maintainerLoop, .{self});
    }

    pub fn stop(self: *ConnectionPool) void {
        self.running.store(false, .release);
        self.consumed.set(); // Wake up maintainer so it can exit
        if (self.thread) |t| {
            t.join();
            self.thread = null;
        }
        self.mutex.lock();
        defer self.mutex.unlock();
        if (self.spare) |*ws| {
            ws.close();
            self.spare = null;
        }
    }

    pub fn take(self: *ConnectionPool) ?WebSocket {
        self.mutex.lock();
        defer self.mutex.unlock();
        const ws = self.spare;
        if (ws != null) {
            self.spare = null;
            self.consumed.set();
        }
        return ws;
    }

    fn maintainerLoop(self: *ConnectionPool) void {
        log.info("Connection pool maintainer started", .{});
        while (self.running.load(.acquire)) {
            const needs_conn = blk: {
                self.mutex.lock();
                defer self.mutex.unlock();
                break :blk self.spare == null;
            };

            if (needs_conn) {
                log.info("Pre-connecting to Doubao...", .{});
                if (connectToAsr(self.allocator, &self.cfg)) |ws| {
                    log.info("Pre-connection established. Ready.", .{});
                    self.mutex.lock();
                    defer self.mutex.unlock();
                    self.spare = ws;
                } else |err| {
                    log.err("Pre-connection failed: {any}. Retrying in 3s...", .{err});
                    std.Thread.sleep(3 * std.time.ns_per_s);
                    continue;
                }
            }

            // Wait until consumed
            self.consumed.reset();
            self.consumed.timedWait(30 * std.time.ns_per_s) catch {};
            std.Thread.sleep(100 * std.time.ns_per_ms);
        }
        log.info("Connection pool maintainer stopped", .{});
    }
};

/// Represents an active ASR session. Runs in its own thread.
pub const Session = struct {
    ws: WebSocket,
    allocator: std.mem.Allocator,
    cfg: AsrConfig,
    callback: *const fn (event_type: u32, text: [*c]const u8, user_data: ?*anyopaque) void,
    user_data: ?*anyopaque,
    audio_target: *audio_mod.AudioTarget,
    thread: ?std.Thread,
    running: std.atomic.Value(bool),
    /// Ring buffer for audio chunks from capture thread
    audio_ring: AudioRing,

    pub fn init(
        ws: WebSocket,
        allocator: std.mem.Allocator,
        cfg: AsrConfig,
        callback: *const fn (event_type: u32, text: [*c]const u8, user_data: ?*anyopaque) void,
        user_data: ?*anyopaque,
        audio_target: *audio_mod.AudioTarget,
    ) Session {
        return Session{
            .ws = ws,
            .allocator = allocator,
            .cfg = cfg,
            .callback = callback,
            .user_data = user_data,
            .audio_target = audio_target,
            .thread = null,
            .running = std.atomic.Value(bool).init(false),
            .audio_ring = .{},
        };
    }

    pub fn start(self: *Session) !void {
        self.running.store(true, .release);
        // Set audio target to route audio into our ring buffer
        self.audio_target.setTarget(audioCallback, @ptrCast(self));
        self.thread = try std.Thread.spawn(.{}, sessionLoop, .{self});
    }

    pub fn stopAudio(self: *Session) void {
        self.audio_target.clearTarget();
    }

    pub fn cancel(self: *Session) void {
        self.running.store(false, .release);
        self.audio_target.clearTarget();
    }

    pub fn join(self: *Session) void {
        if (self.thread) |t| {
            t.join();
            self.thread = null;
        }
    }

    fn audioCallback(data: []const u8, user_data: ?*anyopaque) void {
        const self: *Session = @ptrCast(@alignCast(user_data.?));
        self.audio_ring.push(data);
    }

    fn emitEvent(self: *Session, event_type: u32, text: []const u8) void {
        // Create null-terminated copy
        var buf: [4096]u8 = undefined;
        const len = @min(text.len, buf.len - 1);
        @memcpy(buf[0..len], text[0..len]);
        buf[len] = 0;
        self.callback(event_type, &buf, self.user_data);
    }

    fn sessionLoop(self: *Session) void {
        self.runSession() catch |err| {
            log.err("Session error: {any}", .{err});
            self.emitEvent(3, "session error"); // ANYTALK_EVENT_ERROR
        };
        // Session done - emit idle status
        self.emitEvent(2, "idle"); // ANYTALK_EVENT_STATUS
    }

    fn runSession(self: *Session) !void {
        log.info("Starting ASR session", .{});

        // Set a read timeout so the loop can check the running flag periodically
        self.ws.tls.setReadTimeout(200);

        // Send initial request JSON
        const req_json = try json_mod.buildInitialRequest(self.allocator, self.cfg.mode);
        defer self.allocator.free(req_json);

        const initial_frame = try protocol.buildFullClientRequest(self.allocator, req_json);
        defer self.allocator.free(initial_frame);

        self.ws.sendBinary(initial_frame) catch |err| {
            log.err("Failed to send initial request: {any}", .{err});
            return err;
        };

        var last_committed_end_time: i64 = -1;
        var last_full_text: []const u8 = "";
        var chunk_count: u32 = 0;
        var audio_done = false;

        while (self.running.load(.acquire)) {
            // Try to read audio and send it
            if (!audio_done) {
                if (self.audio_ring.pop()) |chunk| {
                    chunk_count += 1;
                    if (chunk_count % 20 == 0) {
                        log.info("Sent {d} audio chunks to ASR...", .{chunk_count});
                    }
                    const frame = protocol.buildAudioOnlyRequest(self.allocator, &chunk, false) catch continue;
                    defer self.allocator.free(frame);
                    self.ws.sendBinary(frame) catch {
                        audio_done = true;
                    };
                } else {
                    // No audio available, check if target was cleared (stop signal)
                    if (!self.audio_target.isActive()) {
                        log.info("Audio source stopped (Stop received)", .{});
                        // Send empty final frame
                        const final_frame = protocol.buildAudioOnlyRequest(self.allocator, &.{}, true) catch {
                            audio_done = true;
                            continue;
                        };
                        defer self.allocator.free(final_frame);
                        self.ws.sendBinary(final_frame) catch {};
                        audio_done = true;
                    }
                }
            }

            // Try to read WebSocket response (200ms SO_RCVTIMEO on socket)
            const frame = self.ws.readFrame() catch |err| {
                switch (err) {
                    error.WouldBlock => continue, // Timeout, check running flag
                    error.ConnectionClosed => {
                        log.info("WebSocket closed", .{});
                        break;
                    },
                    else => {
                        log.err("WebSocket read error: {any}", .{err});
                        break;
                    },
                }
            };
            defer self.allocator.free(frame.payload);

            switch (frame.opcode) {
                .binary => {
                    const parsed = protocol.parseServerMessage(frame.payload);
                    if (parsed.kind == .@"error") {
                        const msg = parsed.error_msg orelse "server error";
                        log.err("ASR Error: {s}", .{msg});
                        self.emitEvent(3, msg);
                        break;
                    }
                    if (parsed.kind != .response) continue;
                    if (parsed.json_text) |jt| {
                        const result = json_mod.parseAsrResponse(
                            self.allocator,
                            jt,
                            &last_committed_end_time,
                            &last_full_text,
                            self.cfg.mode,
                        ) catch continue;

                        if (result.partial) |p| {
                            self.emitEvent(0, p); // PARTIAL
                            self.allocator.free(p);
                        }
                        for (result.finals) |f| {
                            self.emitEvent(1, f); // FINAL
                            self.allocator.free(f);
                        }
                        self.allocator.free(result.finals);

                        // 0b0011 means final server response frame
                        if (parsed.flags == 0b0011) {
                            log.info("Received final server response frame. Closing.", .{});
                            break;
                        }
                    }
                },
                .close => {
                    log.info("WebSocket closed by server", .{});
                    break;
                },
                .ping => {
                    self.ws.sendPong(frame.payload) catch {};
                },
                else => {},
            }
        }

        if (last_full_text.len > 0) {
            self.allocator.free(last_full_text);
        }

        self.ws.close();
    }
};

/// Simple lock-free ring buffer for audio chunks.
/// Each slot holds one 6400-byte chunk.
const AudioRing = struct {
    const SLOTS = 32;
    slots: [SLOTS][audio_mod.CHUNK_BYTES]u8 = undefined,
    write_pos: std.atomic.Value(usize) = std.atomic.Value(usize).init(0),
    read_pos: std.atomic.Value(usize) = std.atomic.Value(usize).init(0),

    fn push(self: *AudioRing, data: []const u8) void {
        const wp = self.write_pos.load(.acquire);
        const rp = self.read_pos.load(.acquire);
        // Drop if full
        if ((wp + 1) % SLOTS == rp) return;
        const slot = wp % SLOTS;
        const len = @min(data.len, audio_mod.CHUNK_BYTES);
        @memcpy(self.slots[slot][0..len], data[0..len]);
        self.write_pos.store((wp + 1) % SLOTS, .release);
    }

    fn pop(self: *AudioRing) ?[audio_mod.CHUNK_BYTES]u8 {
        const rp = self.read_pos.load(.acquire);
        const wp = self.write_pos.load(.acquire);
        if (rp == wp) return null;
        const slot = rp % SLOTS;
        const data = self.slots[slot];
        self.read_pos.store((rp + 1) % SLOTS, .release);
        return data;
    }
};
