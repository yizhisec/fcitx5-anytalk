const std = @import("std");
const log = std.log.scoped(.audio);

const c = @cImport({
    @cInclude("pulse/simple.h");
    @cInclude("pulse/error.h");
});

/// Audio chunk: 40ms at 16kHz mono S16LE = 640 samples = 1280 bytes
pub const CHUNK_BYTES: usize = 1280;

/// Compute normalized RMS amplitude (~0..1) from S16LE PCM bytes.
pub fn rmsLevel(data: []const u8) f32 {
    if (data.len < 2) return 0.0;
    const sample_count = data.len / 2;
    var sum_sq: f64 = 0.0;
    var i: usize = 0;
    while (i + 1 < data.len) : (i += 2) {
        const lo: i16 = @bitCast(@as(u16, data[i]) | (@as(u16, data[i + 1]) << 8));
        const v: f64 = @as(f64, @floatFromInt(lo)) / 32768.0;
        sum_sq += v * v;
    }
    const rms = std.math.sqrt(sum_sq / @as(f64, @floatFromInt(sample_count)));
    // Map RMS ~[0, 0.4] to [0, 1] visually (typical voice rarely fills full scale).
    const scaled = rms / 0.4;
    return @floatCast(@min(1.0, @max(0.0, scaled)));
}

pub const LevelCallback = *const fn (level: f32, user_data: ?*anyopaque) void;

pub const AudioTarget = struct {
    mutex: std.Thread.Mutex = .{},
    callback: ?*const fn (data: []const u8, user_data: ?*anyopaque) void = null,
    user_data: ?*anyopaque = null,

    // Always-on level callback (independent of session target).
    level_callback: ?LevelCallback = null,
    level_user_data: ?*anyopaque = null,

    pub fn setTarget(self: *AudioTarget, cb: *const fn (data: []const u8, user_data: ?*anyopaque) void, ud: ?*anyopaque) void {
        self.mutex.lock();
        defer self.mutex.unlock();
        self.callback = cb;
        self.user_data = ud;
    }

    pub fn clearTarget(self: *AudioTarget) void {
        self.mutex.lock();
        defer self.mutex.unlock();
        self.callback = null;
        self.user_data = null;
    }

    pub fn setLevelCallback(self: *AudioTarget, cb: LevelCallback, ud: ?*anyopaque) void {
        self.mutex.lock();
        defer self.mutex.unlock();
        self.level_callback = cb;
        self.level_user_data = ud;
    }

    pub fn isActive(self: *AudioTarget) bool {
        self.mutex.lock();
        defer self.mutex.unlock();
        return self.callback != null;
    }

    pub fn send(self: *AudioTarget, data: []const u8) void {
        self.mutex.lock();
        const cb = self.callback;
        const ud = self.user_data;
        const lvl_cb = self.level_callback;
        const lvl_ud = self.level_user_data;
        self.mutex.unlock();

        // Always feed the level callback so UI can show idle baseline movement
        // even when no session is consuming audio.
        if (lvl_cb) |fn_| {
            fn_(rmsLevel(data), lvl_ud);
        }
        if (cb) |fn_| {
            fn_(data, ud);
        }
    }
};

pub const AudioCapture = struct {
    pa: ?*c.pa_simple = null,
    thread: ?std.Thread = null,
    running: std.atomic.Value(bool) = std.atomic.Value(bool).init(false),
    target: *AudioTarget,

    pub fn init(target: *AudioTarget) AudioCapture {
        return .{ .target = target };
    }

    pub fn start(self: *AudioCapture) !void {
        if (self.running.load(.acquire)) return;

        var pa_err: c_int = 0;
        const buf_attr = c.pa_buffer_attr{
            .maxlength = std.math.maxInt(u32),
            .tlength = std.math.maxInt(u32),
            .prebuf = std.math.maxInt(u32),
            .minreq = std.math.maxInt(u32),
            .fragsize = CHUNK_BYTES, // Minimize capture latency
        };
        self.pa = c.pa_simple_new(
            null,
            "anytalk",
            c.PA_STREAM_RECORD,
            null,
            "Voice Input",
            &c.pa_sample_spec{ .format = c.PA_SAMPLE_S16LE, .rate = 16000, .channels = 1 },
            null,
            &buf_attr,
            &pa_err,
        );
        if (self.pa == null) {
            log.err("pa_simple_new failed: {s}", .{c.pa_strerror(pa_err)});
            return error.PulseAudioFailed;
        }
        errdefer {
            c.pa_simple_free(self.pa.?);
            self.pa = null;
        }

        self.running.store(true, .release);
        self.thread = try std.Thread.spawn(.{}, captureLoop, .{self});
    }

    pub fn stop(self: *AudioCapture) void {
        self.running.store(false, .release);
        if (self.thread) |t| {
            t.join();
            self.thread = null;
        }
        if (self.pa) |pa| {
            c.pa_simple_free(pa);
            self.pa = null;
        }
    }

    fn captureLoop(self: *AudioCapture) void {
        var buf: [CHUNK_BYTES]u8 = undefined;
        log.info("Audio capture thread started", .{});
        while (self.running.load(.acquire)) {
            var pa_err: c_int = 0;
            if (c.pa_simple_read(self.pa.?, &buf, buf.len, &pa_err) < 0) {
                log.err("pa_simple_read failed: {s}", .{c.pa_strerror(pa_err)});
                break;
            }
            self.target.send(&buf);
        }
        log.info("Audio capture thread stopped", .{});
    }
};
