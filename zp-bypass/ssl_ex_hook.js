'use strict';

// Full KeyAuth response — covers init, license, check, var, log, file requests
function fakeResponse(reqBody) {
    var base = {
        success: true,
        message: "Logged in.",
        sessionid: "bypass_session_001",
        info: {
            username: "activeuser",
            subscriptions: [{
                subscription: "default",
                key: "BYPASS-KEY-0001",
                expiry: "9999999999",
                timeleft: 999999999,
                level: "1"
            }],
            ip: "1.1.1.1",
            hwid: "bypass",
            createdate: "1700000000",
            lastlogin: "1700000000",
            cooldown: "0",
            level: "1"
        }
    };
    // type=var → return variable value
    if (reqBody.indexOf('type=var') !== -1) {
        base.message = "Variable grabbed";
        base.response = "1";
    }
    // type=log → ack
    if (reqBody.indexOf('type=log') !== -1) {
        base.message = "Logged";
    }
    // type=check → session still valid
    if (reqBody.indexOf('type=check') !== -1) {
        base.message = "Session validated";
    }
    // type=file → return empty file data
    if (reqBody.indexOf('type=file') !== -1) {
        base.message = "File grabbed";
        base.contents = "";
    }
    return JSON.stringify(base);
}

var kaSSL = {};
var done  = {};

function installHooks() {
    var mod = Process.findModuleByName('libssl-3-x64.dll');
    if (!mod) return false;
    var SSL_write_ex = mod.getExportByName('SSL_write_ex');
    var SSL_read_ex  = mod.getExportByName('SSL_read_ex');
    var SSL_read     = mod.getExportByName('SSL_read');
    send('[+] libssl-3-x64.dll found — installing hooks');

    Interceptor.attach(SSL_write_ex, {
    onEnter: function(a) {
        this.ssl = a[0].toString();
        var len = a[2].toInt32();
        if (len > 0 && len < 8192) {
            try {
                var bytes = new Uint8Array(a[1].readByteArray(Math.min(len, 800)));
                var s = '';
                for (var i = 0; i < bytes.length; i++)
                    s += (bytes[i] >= 32 && bytes[i] < 127) ? String.fromCharCode(bytes[i]) : '.';
                if (s.indexOf('keyauth') !== -1 || s.indexOf('type=') !== -1 || s.indexOf('ownerid') !== -1) {
                    kaSSL[this.ssl] = s;
                    done[this.ssl] = false;
                    send('REQUEST: ' + s.substring(0, 400));
                }
            } catch(e) {}
        }
    }
});

Interceptor.attach(SSL_read_ex, {
    onEnter: function(a) {
        this.ssl = a[0].toString();
        this.buf = a[1];
        this.sz  = a[2].toInt32();
        this.rb  = a[3];
        this.ka  = (kaSSL[this.ssl] !== undefined);
    },
    onLeave: function(r) {
        if (!this.ka) return;
        if (done[this.ssl]) { r.replace(ptr(0)); return; }
        done[this.ssl] = true;
        var body = fakeResponse(kaSSL[this.ssl] || '');
        send('INJECTING for: ' + (kaSSL[this.ssl] || '').substring(0, 80));
        var http = 'HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: ' + body.length + '\r\nConnection: close\r\n\r\n' + body;
        var b = [];
        for (var i = 0; i < http.length; i++) b.push(http.charCodeAt(i) & 0xff);
        var n = Math.min(b.length, this.sz);
        this.buf.writeByteArray(b.slice(0, n));
        if (this.rb && !this.rb.isNull()) this.rb.writeU64(n);
        r.replace(ptr(1));
    }
});

Interceptor.attach(SSL_read, {
    onEnter: function(a) {
        this.ssl = a[0].toString();
        this.buf = a[1];
        this.sz  = a[2].toInt32();
        this.ka  = (kaSSL[this.ssl] !== undefined);
    },
    onLeave: function(r) {
        if (!this.ka || done[this.ssl]) return;
        done[this.ssl] = true;
        var body = fakeResponse(kaSSL[this.ssl] || '');
        send('INJECTING(read) for: ' + (kaSSL[this.ssl] || '').substring(0, 80));
        var http = 'HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: ' + body.length + '\r\nConnection: close\r\n\r\n' + body;
        var b = [];
        for (var i = 0; i < http.length; i++) b.push(http.charCodeAt(i) & 0xff);
        var n = Math.min(b.length, this.sz);
        this.buf.writeByteArray(b.slice(0, n));
        r.replace(ptr(n));
    }
});

    send('[+] Full KeyAuth bypass active — all request types covered');
    return true;
}

// Poll until libssl-3-x64.dll loads (it loads when Python imports ssl/requests)
var _installed = false;
var _pollId = setInterval(function() {
    if (_installed) { clearInterval(_pollId); return; }
    if (installHooks()) {
        _installed = true;
        clearInterval(_pollId);
    }
}, 200);

