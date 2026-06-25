#include "SshClient.hpp"

#if ENABLE_SSH
#include <libssh_esp32.h>
#include <libssh/libssh.h>
#endif

extern void tab5SetCrashStage(const char* stage);

bool SshClient::connect(const SshProfile& profile, String& error, int columns, int rows)
{
#if ENABLE_SSH
    tab5SetCrashStage("ssh.libssh_begin");
    static bool libsshStarted = false;
    if (!libsshStarted) {
        libssh_begin();
        libsshStarted = true;
    }

    tab5SetCrashStage("ssh.disconnect");
    disconnect();

    tab5SetCrashStage("ssh_new");
    ssh_session session = ssh_new();
    if (!session) {
        error = "ssh_new failed";
        return false;
    }

    tab5SetCrashStage("ssh_options");
    const int verbosity = SSH_LOG_NOLOG;
    ssh_options_set(session, SSH_OPTIONS_HOST, profile.host.c_str());
    const int port = profile.port;
    ssh_options_set(session, SSH_OPTIONS_PORT, &port);
    ssh_options_set(session, SSH_OPTIONS_USER, profile.user.c_str());
    ssh_options_set(session, SSH_OPTIONS_LOG_VERBOSITY, &verbosity);

    tab5SetCrashStage("ssh_connect");
    if (ssh_connect(session) != SSH_OK) {
        error = ssh_get_error(session);
        ssh_free(session);
        return false;
    }

    tab5SetCrashStage("ssh_auth_password");
    int auth = ssh_userauth_password(session, nullptr, profile.password.c_str());
    if (auth != SSH_AUTH_SUCCESS) {
        error = ssh_get_error(session);
        ssh_disconnect(session);
        ssh_free(session);
        return false;
    }

    tab5SetCrashStage("ssh_channel_new");
    ssh_channel channel = ssh_channel_new(session);
    if (!channel) {
        error = "ssh_channel_new failed";
        ssh_disconnect(session);
        ssh_free(session);
        return false;
    }

    tab5SetCrashStage("ssh_pty_shell");
    if (ssh_channel_open_session(channel) != SSH_OK ||
        ssh_channel_request_pty_size(channel, profile.terminal.c_str(), columns, rows) != SSH_OK ||
        ssh_channel_request_shell(channel) != SSH_OK) {
        error = ssh_get_error(session);
        ssh_channel_free(channel);
        ssh_disconnect(session);
        ssh_free(session);
        return false;
    }

    ssh_set_blocking(session, 0);
    _session = session;
    _channel = channel;
    tab5SetCrashStage("ssh_ready");
    return true;
#else
    (void)profile;
    (void)columns;
    (void)rows;
    error = "ENABLE_SSH is disabled";
    return false;
#endif
}

void SshClient::disconnect()
{
#if ENABLE_SSH
    if (_channel) {
        ssh_channel channel = static_cast<ssh_channel>(_channel);
        ssh_channel_send_eof(channel);
        ssh_channel_close(channel);
        ssh_channel_free(channel);
        _channel = nullptr;
    }
    if (_session) {
        ssh_session session = static_cast<ssh_session>(_session);
        ssh_disconnect(session);
        ssh_free(session);
        _session = nullptr;
    }
#endif
}

bool SshClient::connected() const
{
#if ENABLE_SSH
    return _session && _channel && !ssh_channel_is_closed(static_cast<ssh_channel>(_channel));
#else
    return false;
#endif
}

int SshClient::read(char* buffer, size_t len)
{
#if ENABLE_SSH
    if (!connected()) {
        return -1;
    }
    int n = ssh_channel_read_nonblocking(static_cast<ssh_channel>(_channel), buffer, len, 0);
    if (n == SSH_AGAIN) {
        return 0;
    }
    return n == SSH_ERROR ? -1 : n;
#else
    (void)buffer;
    (void)len;
    return -1;
#endif
}

bool SshClient::write(const uint8_t* data, size_t len)
{
#if ENABLE_SSH
    if (!connected()) {
        return false;
    }
    int n = ssh_channel_write(static_cast<ssh_channel>(_channel), data, len);
    return n == static_cast<int>(len);
#else
    (void)data;
    (void)len;
    return false;
#endif
}

bool SshClient::resizePty(int columns, int rows)
{
#if ENABLE_SSH
    if (!connected()) {
        return false;
    }
    return ssh_channel_change_pty_size(static_cast<ssh_channel>(_channel), columns, rows) == SSH_OK;
#else
    (void)columns;
    (void)rows;
    return false;
#endif
}
