const std = @import("std");

pub fn build(b: *std.Build) void {
    const target = b.standardTargetOptions(.{});
    const optimize = b.standardOptimizeOption(.{});

    const lib = b.addLibrary(.{
        .linkage = .dynamic,
        .name = "anytalk",
        .root_module = b.createModule(.{
            .root_source_file = b.path("src/api.zig"),
            .target = target,
            .optimize = optimize,
            .link_libc = true,
        }),
    });

    // Link system libraries
    lib.linkSystemLibrary("pulse-simple");
    lib.linkSystemLibrary("pulse");
    lib.linkSystemLibrary("ssl");
    lib.linkSystemLibrary("crypto");

    // Install the shared library
    b.installArtifact(lib);
}
