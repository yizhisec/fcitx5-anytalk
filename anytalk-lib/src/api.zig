const std = @import("std");
const context = @import("context.zig");
const log = std.log.scoped(.api);

const AnytalkContext = context.AnytalkContext;

// C ABI callback type - must use callconv(.c) for extern functions
const AnytalkEventCallback = *const fn (user_data: ?*anyopaque, event_type: u32, text: [*c]const u8) callconv(.c) void;

const AnytalkConfig = extern struct {
    app_id: [*c]const u8,
    access_token: [*c]const u8,
    resource_id: [*c]const u8, // NULL = default
    mode: [*c]const u8, // NULL = default "bidi_async"
};

fn cstrToSlice(s: [*c]const u8) []const u8 {
    if (s == null) return "";
    return std.mem.span(s);
}

export fn anytalk_init(
    config: *const AnytalkConfig,
    cb: AnytalkEventCallback,
    user_data: ?*anyopaque,
) callconv(.c) ?*AnytalkContext {
    const app_id = cstrToSlice(config.app_id);
    const access_token = cstrToSlice(config.access_token);
    const resource_id_raw = cstrToSlice(config.resource_id);
    const resource_id = if (resource_id_raw.len == 0) "volc.seedasr.sauc.duration" else resource_id_raw;
    const mode_raw = cstrToSlice(config.mode);
    const mode = if (mode_raw.len == 0) "bidi_async" else mode_raw;

    log.info("anytalk_init called (app_id={s}, mode={s})", .{ app_id, mode });

    return AnytalkContext.init(app_id, access_token, resource_id, mode, cb, user_data) catch |err| {
        log.err("anytalk_init failed: {any}", .{err});
        return null;
    };
}

export fn anytalk_destroy(ctx: ?*AnytalkContext) callconv(.c) void {
    if (ctx) |c2| {
        log.info("anytalk_destroy called", .{});
        c2.destroy();
    }
}

export fn anytalk_start(ctx: ?*AnytalkContext) callconv(.c) c_int {
    const c2 = ctx orelse return -1;
    log.info("anytalk_start called", .{});
    c2.startSession() catch |err| {
        log.err("anytalk_start failed: {any}", .{err});
        return -1;
    };
    return 0;
}

export fn anytalk_stop(ctx: ?*AnytalkContext) callconv(.c) c_int {
    const c2 = ctx orelse return -1;
    log.info("anytalk_stop called", .{});
    c2.stopSession();
    return 0;
}

export fn anytalk_cancel(ctx: ?*AnytalkContext) callconv(.c) c_int {
    const c2 = ctx orelse return -1;
    log.info("anytalk_cancel called", .{});
    c2.cancelInternal();
    return 0;
}
