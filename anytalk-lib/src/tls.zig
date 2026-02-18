const std = @import("std");
const log = std.log.scoped(.tls);

const c = @cImport({
    @cInclude("openssl/ssl.h");
    @cInclude("openssl/err.h");
    @cInclude("openssl/x509v3.h");
    @cInclude("netdb.h");
    @cInclude("netinet/in.h");
    @cInclude("sys/socket.h");
    @cInclude("unistd.h");
    @cInclude("errno.h");
});

pub const TlsStream = struct {
    ssl: *c.SSL,
    ctx: *c.SSL_CTX,
    fd: c_int,

    pub fn connect(host: []const u8, port: u16) !TlsStream {
        // Create SSL context
        const method = c.TLS_client_method() orelse return error.SslMethodFailed;
        const ctx = c.SSL_CTX_new(method) orelse return error.SslCtxFailed;
        errdefer c.SSL_CTX_free(ctx);

        // Load default CA certificates
        if (c.SSL_CTX_set_default_verify_paths(ctx) != 1) {
            return error.SslCaPathFailed;
        }

        // Resolve host
        var host_z: [256]u8 = undefined;
        if (host.len >= host_z.len) return error.HostNameTooLong;
        @memcpy(host_z[0..host.len], host);
        host_z[host.len] = 0;

        var port_z: [8]u8 = undefined;
        const port_str = std.fmt.bufPrint(&port_z, "{d}", .{port}) catch return error.PortFmtFailed;
        port_z[port_str.len] = 0;

        var hints: c.struct_addrinfo = std.mem.zeroes(c.struct_addrinfo);
        hints.ai_family = c.AF_UNSPEC;
        hints.ai_socktype = c.SOCK_STREAM;

        var res: ?*c.struct_addrinfo = null;
        const gai_ret = c.getaddrinfo(&host_z, &port_z, &hints, &res);
        if (gai_ret != 0 or res == null) return error.DnsResolveFailed;
        defer c.freeaddrinfo(res.?);

        // Connect TCP
        const ai = res.?;
        const fd = c.socket(ai.ai_family, ai.ai_socktype, ai.ai_protocol);
        if (fd < 0) return error.SocketCreateFailed;
        errdefer _ = c.close(fd);

        if (c.connect(fd, ai.ai_addr, ai.ai_addrlen) < 0) {
            return error.TcpConnectFailed;
        }

        // SSL handshake
        const ssl = c.SSL_new(ctx) orelse return error.SslNewFailed;
        errdefer c.SSL_free(ssl);

        // Set SNI hostname
        if (c.SSL_set_tlsext_host_name(ssl, &host_z) != 1) {
            return error.SslSniFailed;
        }

        // Enable hostname verification
        const param = c.SSL_get0_param(ssl);
        _ = c.X509_VERIFY_PARAM_set1_host(param, &host_z, host.len);
        c.SSL_set_verify(ssl, c.SSL_VERIFY_PEER, null);

        if (c.SSL_set_fd(ssl, fd) != 1) return error.SslSetFdFailed;
        if (c.SSL_connect(ssl) != 1) return error.SslHandshakeFailed;

        return TlsStream{
            .ssl = ssl,
            .ctx = ctx,
            .fd = fd,
        };
    }

    /// Set a read timeout on the underlying socket. 0 means no timeout.
    pub fn setReadTimeout(self: *TlsStream, ms: u32) void {
        const tv = c.struct_timeval{
            .tv_sec = @intCast(ms / 1000),
            .tv_usec = @intCast(@as(u64, ms % 1000) * 1000),
        };
        _ = c.setsockopt(self.fd, c.SOL_SOCKET, c.SO_RCVTIMEO, &tv, @sizeOf(c.struct_timeval));
    }

    pub fn read(self: *TlsStream, buf: []u8) !usize {
        const n = c.SSL_read(self.ssl, buf.ptr, @intCast(buf.len));
        if (n <= 0) {
            const err = c.SSL_get_error(self.ssl, n);
            if (err == c.SSL_ERROR_ZERO_RETURN) return error.ConnectionClosed;
            if (err == c.SSL_ERROR_WANT_READ) return error.WouldBlock;
            if (err == c.SSL_ERROR_SYSCALL) {
                const e = std.c._errno().*;
                if (e == c.EAGAIN or e == c.EWOULDBLOCK) return error.WouldBlock;
            }
            return error.SslReadFailed;
        }
        return @intCast(n);
    }

    pub fn write(self: *TlsStream, data: []const u8) !void {
        var sent: usize = 0;
        while (sent < data.len) {
            const n = c.SSL_write(self.ssl, data[sent..].ptr, @intCast(data.len - sent));
            if (n <= 0) return error.SslWriteFailed;
            sent += @as(usize, @intCast(n));
        }
    }

    pub fn close(self: *TlsStream) void {
        _ = c.SSL_shutdown(self.ssl);
        c.SSL_free(self.ssl);
        c.SSL_CTX_free(self.ctx);
        _ = c.close(self.fd);
    }
};
