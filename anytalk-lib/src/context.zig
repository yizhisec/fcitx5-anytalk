const std = @import("std");
const audio_mod = @import("audio.zig");
const asr_mod = @import("asr.zig");
const log = std.log.scoped(.context);

pub const AnytalkContext = struct {
    allocator: std.mem.Allocator,
    config: asr_mod.AsrConfig,
    callback: *const fn (user_data: ?*anyopaque, event_type: u32, text: [*c]const u8) callconv(.c) void,
    user_data: ?*anyopaque,

    // Audio subsystem
    audio_target: audio_mod.AudioTarget,
    audio_capture: audio_mod.AudioCapture,

    // Audio level throttling (~20 Hz)
    level_emit_mutex: std.Thread.Mutex = .{},
    last_level_emit_ns: i128 = 0,

    // Connection pool
    pool: asr_mod.ConnectionPool,

    // Active session
    session_mutex: std.Thread.Mutex = .{},
    session: ?*asr_mod.Session = null,
    draining_session: ?*asr_mod.Session = null,
    drain_thread: ?std.Thread = null,

    pub fn init(
        app_id: []const u8,
        access_token: []const u8,
        resource_id: []const u8,
        mode: []const u8,
        callback: *const fn (user_data: ?*anyopaque, event_type: u32, text: [*c]const u8) callconv(.c) void,
        user_data: ?*anyopaque,
    ) !*AnytalkContext {
        const allocator = std.heap.c_allocator;

        // Make owned copies of config strings
        const app_id_buf = try allocator.dupe(u8, app_id);
        errdefer allocator.free(app_id_buf);
        const access_token_buf = try allocator.dupe(u8, access_token);
        errdefer allocator.free(access_token_buf);
        const resource_id_buf = try allocator.dupe(u8, resource_id);
        errdefer allocator.free(resource_id_buf);
        const mode_buf = try allocator.dupe(u8, mode);
        errdefer allocator.free(mode_buf);

        const self = try allocator.create(AnytalkContext);
        errdefer allocator.destroy(self);

        const cfg = asr_mod.AsrConfig{
            .app_id = app_id_buf,
            .access_token = access_token_buf,
            .resource_id = resource_id_buf,
            .mode = mode_buf,
        };

        self.* = AnytalkContext{
            .allocator = allocator,
            .config = cfg,
            .callback = callback,
            .user_data = user_data,
            .audio_target = .{},
            .audio_capture = undefined, // Initialized below
            .pool = asr_mod.ConnectionPool.init(allocator, cfg),
        };

        self.audio_capture = audio_mod.AudioCapture.init(&self.audio_target);

        // Wire audio level callback → main user callback (throttled).
        const opaque_self: *anyopaque = @ptrCast(self);
        self.audio_target.setLevelCallback(levelCallback, opaque_self);

        // Start audio capture
        self.audio_capture.start() catch |err| {
            log.err("Failed to start audio capture: {any}", .{err});
            // Continue anyway - audio will fail gracefully
        };

        // Start connection pool maintainer
        self.pool.start() catch |err| {
            log.err("Failed to start connection pool: {any}", .{err});
        };

        log.info("AnytalkContext initialized (mode={s})", .{mode_buf});
        return self;
    }

    pub fn destroy(self: *AnytalkContext) void {
        log.info("Destroying AnytalkContext", .{});

        // Cancel any active session
        self.cancelInternal();

        // Stop pool and audio
        self.pool.stop();
        self.audio_capture.stop();

        // Free config strings
        self.allocator.free(self.config.app_id);
        self.allocator.free(self.config.access_token);
        self.allocator.free(self.config.resource_id);
        self.allocator.free(self.config.mode);

        self.allocator.destroy(self);
    }

    fn sessionCallback(event_type: u32, text: [*c]const u8, user_data: ?*anyopaque) void {
        const self: *AnytalkContext = @ptrCast(@alignCast(user_data.?));
        self.callback(self.user_data, event_type, text);
    }

    /// Audio capture level callback. Throttles to ~20 Hz and emits ANYTALK_EVENT_LEVEL (4)
    /// with payload formatted as decimal string in [0,1] e.g. "0.7400".
    fn levelCallback(level: f32, user_data: ?*anyopaque) void {
        const self: *AnytalkContext = @ptrCast(@alignCast(user_data orelse return));

        // Throttle ~20 Hz.
        const now = std.time.nanoTimestamp();
        self.level_emit_mutex.lock();
        const last = self.last_level_emit_ns;
        if (now - last < 50_000_000) {
            self.level_emit_mutex.unlock();
            return;
        }
        self.last_level_emit_ns = now;
        self.level_emit_mutex.unlock();

        // Hand-rolled "0.NNNN" formatter — avoids std.fmt entry points whose
        // ABI shifted in Zig 0.15 (panicked under ReleaseFast in this addon).
        const clamped = if (level < 0.0) @as(f32, 0.0) else if (level > 1.0) @as(f32, 1.0) else level;
        const scaled: u32 = @intFromFloat(clamped * 10000.0);
        var buf: [8]u8 = undefined;
        buf[0] = '0' + @as(u8, @intCast(scaled / 10000));
        buf[1] = '.';
        buf[2] = '0' + @as(u8, @intCast((scaled / 1000) % 10));
        buf[3] = '0' + @as(u8, @intCast((scaled / 100) % 10));
        buf[4] = '0' + @as(u8, @intCast((scaled / 10) % 10));
        buf[5] = '0' + @as(u8, @intCast(scaled % 10));
        buf[6] = 0;
        buf[7] = 0;
        self.callback(self.user_data, 4, @ptrCast(&buf[0]));
    }

    /// Abort the draining session if one exists. Caller must hold session_mutex.
    /// Temporarily releases and re-acquires the mutex to join the drain thread
    /// (drainWait also acquires session_mutex, so holding it here would deadlock).
    fn abortDraining(self: *AnytalkContext) void {
        const ds = self.draining_session orelse return;
        const dt = self.drain_thread;

        ds.cancel();

        if (dt) |t| {
            self.session_mutex.unlock();
            t.join();
            self.session_mutex.lock();
            self.drain_thread = null;
        }

        // drainWait may have already cleaned up
        if (self.draining_session) |remaining| {
            self.allocator.destroy(remaining);
            self.draining_session = null;
        }
    }

    pub fn startSession(self: *AnytalkContext) !void {
        self.session_mutex.lock();
        defer self.session_mutex.unlock();

        self.abortDraining();

        // Retry audio capture startup in case PulseAudio/pipewire was not ready at init.
        self.audio_capture.start() catch |err| {
            log.err("Failed to start audio capture: {any}", .{err});
            // ERROR drives the toast; the upper layer translates that into a
            // transient visual state and an idle return. We deliberately do
            // NOT emit STATUS="idle" here — the addon/overlay handles the
            // 1.8s read-window for the toast and emits its own idle when the
            // window expires. Emitting idle here would tear down the toast
            // immediately.
            self.callback(self.user_data, 3,
                "麦克风不可用，请检查 PulseAudio/PipeWire 或音频设备");
            return err;
        };

        // Abort active session
        if (self.session) |s| {
            s.cancel();
            s.join();
            self.allocator.destroy(s);
            self.session = null;
        }

        // Get or create WebSocket connection
        var ws = self.pool.take();
        if (ws == null) {
            log.info("No hot spare, connecting on demand...", .{});
            // Emit connecting status
            self.callback(self.user_data, 2, "connecting");
            ws = asr_mod.connectToAsr(self.allocator, &self.config) catch |err| {
                log.err("Connection failed: {any}", .{err});
                self.callback(self.user_data, 3, "connection failed");
                return err;
            };
        }

        // Create new session
        const session = try self.allocator.create(asr_mod.Session);
        session.* = asr_mod.Session.init(
            ws.?,
            self.allocator,
            self.config,
            sessionCallback,
            @ptrCast(self),
            &self.audio_target,
            &self.pool,
        );

        try session.start();
        self.session = session;

        // Emit recording status
        self.callback(self.user_data, 2, "recording");
    }

    pub fn stopSession(self: *AnytalkContext) void {
        self.session_mutex.lock();
        defer self.session_mutex.unlock();

        self.abortDraining();

        if (self.session) |s| {
            // Stop audio flow but let session drain
            s.stopAudio();
            self.draining_session = s;
            self.session = null;

            // Spawn a thread to wait for the draining session to finish
            self.drain_thread = std.Thread.spawn(.{}, drainWait, .{ self, s }) catch null;
        } else {
            self.callback(self.user_data, 2, "idle");
        }
    }

    fn drainWait(self: *AnytalkContext, s: *asr_mod.Session) void {
        s.join();
        self.session_mutex.lock();
        defer self.session_mutex.unlock();
        if (self.draining_session == s) {
            self.allocator.destroy(s);
            self.draining_session = null;
        }
    }

    pub fn cancelInternal(self: *AnytalkContext) void {
        self.session_mutex.lock();
        defer self.session_mutex.unlock();

        if (self.session) |s| {
            s.cancel();
            s.join();
            self.allocator.destroy(s);
            self.session = null;
        }

        self.abortDraining();
        self.callback(self.user_data, 2, "idle");
    }
};
