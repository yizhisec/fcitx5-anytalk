const std = @import("std");

/// Build the initial ASR request JSON for Volcengine.
pub fn buildInitialRequest(allocator: std.mem.Allocator, mode: []const u8) ![]u8 {
    var buf: std.ArrayList(u8) = .{};
    defer buf.deinit(allocator);
    const w = buf.writer(allocator);

    const is_nostream = std.mem.eql(u8, mode, "nostream");

    try w.writeAll(
        \\{"user":{"uid":"anytalk"},"audio":{"format":"pcm","rate":16000,"bits":16,"channel":1
    );
    if (is_nostream) {
        try w.writeAll(
            \\,"language":"zh-CN"
        );
    }
    try w.writeAll(
        \\},"request":{"model_name":"bigmodel","enable_itn":true,"enable_punc":true,"enable_ddc":false,"enable_word":false,"res_type":"full","nbest":1,"use_vad":true}}
    );

    return try buf.toOwnedSlice(allocator);
}

/// Parse ASR response JSON and extract partial/final texts.
pub const AsrParseResult = struct {
    partial: ?[]const u8,
    finals: [][]const u8,
};

pub fn parseAsrResponse(
    allocator: std.mem.Allocator,
    json_text: []const u8,
    last_committed_end_time: *i64,
    last_full_text: *[]const u8,
    mode: []const u8,
) !AsrParseResult {
    const parsed = std.json.parseFromSlice(std.json.Value, allocator, json_text, .{}) catch {
        return AsrParseResult{ .partial = null, .finals = &.{} };
    };
    defer parsed.deinit();

    const root = parsed.value;
    const result_obj = root.object.get("result") orelse {
        return AsrParseResult{ .partial = null, .finals = &.{} };
    };

    var finals_list: std.ArrayList([]const u8) = .{};
    var partial: ?[]const u8 = null;

    // Try utterances array first
    if (result_obj.object.get("utterances")) |utterances_val| {
        if (utterances_val == .array) {
            const utterances = utterances_val.array.items;

            // Collect finals (definite utterances with new end_time)
            for (utterances) |u| {
                if (u != .object) continue;
                const obj = u.object;
                const definite = if (obj.get("definite")) |d| (d == .bool and d.bool) else false;
                if (!definite) continue;
                const end_time: i64 = if (obj.get("end_time")) |et| switch (et) {
                    .integer => |i| i,
                    else => -1,
                } else -1;
                if (end_time <= last_committed_end_time.*) continue;
                if (obj.get("text")) |txt_val| {
                    if (txt_val == .string) {
                        const trimmed = std.mem.trim(u8, txt_val.string, " \t\r\n");
                        if (trimmed.len > 0) {
                            try finals_list.append(allocator, try allocator.dupe(u8, trimmed));
                            last_committed_end_time.* = end_time;
                        }
                    }
                }
            }

            // Find last non-definite utterance for partial
            var i: usize = utterances.len;
            while (i > 0) {
                i -= 1;
                const u = utterances[i];
                if (u != .object) continue;
                const obj = u.object;
                const definite = if (obj.get("definite")) |d| (d == .bool and d.bool) else false;
                if (definite) continue;
                if (obj.get("text")) |txt_val| {
                    if (txt_val == .string) {
                        const trimmed = std.mem.trim(u8, txt_val.string, " \t\r\n");
                        if (trimmed.len > 0) {
                            partial = try allocator.dupe(u8, trimmed);
                            break;
                        }
                    }
                }
            }

            const owned_finals = try finals_list.toOwnedSlice(allocator);
            return AsrParseResult{ .partial = partial, .finals = owned_finals };
        }
    }

    // Fallback: use result.text
    if (result_obj.object.get("text")) |txt_val| {
        if (txt_val == .string) {
            const full = std.mem.trim(u8, txt_val.string, " \t\r\n");
            if (full.len > 0) {
                const full_dupe = try allocator.dupe(u8, full);
                if (std.mem.eql(u8, mode, "bidi_async")) {
                    partial = try allocator.dupe(u8, full);
                    try finals_list.append(allocator, full_dupe);
                } else if (last_full_text.*.len > 0 and std.mem.startsWith(u8, full, last_full_text.*)) {
                    const suffix = std.mem.trim(u8, full[last_full_text.*.len..], " \t\r\n");
                    if (suffix.len > 0) {
                        try finals_list.append(allocator, try allocator.dupe(u8, suffix));
                    }
                    allocator.free(full_dupe);
                } else if (!std.mem.eql(u8, full, last_full_text.*)) {
                    try finals_list.append(allocator, full_dupe);
                } else {
                    allocator.free(full_dupe);
                }
                // Update last_full_text
                if (last_full_text.*.len > 0) {
                    allocator.free(@constCast(last_full_text.*));
                }
                last_full_text.* = try allocator.dupe(u8, full);
            }
        }
    }

    const owned_finals = try finals_list.toOwnedSlice(allocator);
    return AsrParseResult{ .partial = partial, .finals = owned_finals };
}
