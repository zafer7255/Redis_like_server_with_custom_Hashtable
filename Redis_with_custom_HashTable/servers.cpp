#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/ip.h>
#include <string>
#include <vector>
#include <memory>

// proj
#include "hashtable.cpp"

#define container_of(ptr, type, member) ({                  \
    const typeof( ((type *)0)->member ) *__mptr = (ptr);    \
    (type *)( (char *)__mptr - offsetof(type, member) );})

// The container_of macro is a technique often used in low-level and kernel programming where direct manipulation of memory and structures is common. It allows efficient navigation between structure members and their containing structures, facilitating clean and type-safe code.

static void msg(const char *msg) {
    fprintf(stderr, "%s\n", msg);
}

static void die(const char *msg) {
    int err = errno;
    fprintf(stderr, "[%d] %s\n", err, msg);
    abort();
}

static void fd_set_nb(int fd) {
    errno = 0;
    int flags = fcntl(fd, F_GETFL, 0);
    if (errno) {
        die("fcntl error");
        return;
    }

    flags |= O_NONBLOCK;

    errno = 0;
    (void)fcntl(fd, F_SETFL, flags);
    if (errno) {
        die("fcntl error");
    }
}

const size_t k_max_msg = 4096;

enum {
    STATE_REQ = 0,
    STATE_RES = 1,
    STATE_END = 2,  // mark the connection for deletion
};

struct Conn {
    int fd = -1;
    uint32_t state = 0;     // either STATE_REQ or STATE_RES
    // buffer for reading
    size_t rbuf_size = 0;
    uint8_t rbuf[4 + k_max_msg];
    // buffer for writing
    size_t wbuf_size = 0;
    size_t wbuf_sent = 0;
    uint8_t wbuf[4 + k_max_msg];
};

using ConnPtr = std::shared_ptr<Conn>;

static void conn_put(std::vector<ConnPtr> &fd2conn, ConnPtr conn) {
    if (fd2conn.size() <= (size_t)conn->fd) {
        fd2conn.resize(conn->fd + 1);
    }
    fd2conn[conn->fd] = conn;
}

static int32_t accept_new_conn(std::vector<ConnPtr> &fd2conn, int fd) {
    // accept
    struct sockaddr_in client_addr = {};
    socklen_t socklen = sizeof(client_addr);
    int connfd = accept(fd, (struct sockaddr *)&client_addr, &socklen);
    if (connfd < 0) {
        msg("accept() error");
        return -1;  // error
    }

    // set the new connection fd to nonblocking mode
    fd_set_nb(connfd);
    // creating the struct Conn
    ConnPtr conn = std::make_shared<Conn>();
    conn->fd = connfd;
    conn->state = STATE_REQ;
    conn->rbuf_size = 0;
    conn->wbuf_size = 0;
    conn->wbuf_sent = 0;
    conn_put(fd2conn, conn);
    return 0;
}

static void state_req(ConnPtr conn);
static void state_res(ConnPtr conn);

const size_t k_max_args = 1024;

static int32_t parse_req(
    const uint8_t *data, size_t len, std::vector<std::string> &out)
{
    if (len < 4) {
        return -1;
    }
    uint32_t n = 0;
    memcpy(&n, &data[0], 4);
    if (n > k_max_args) {
        return -1;
    }

    size_t pos = 4;
    while (n--) {
        if (pos + 4 > len) {
            return -1;
        }
        uint32_t sz = 0;
        memcpy(&sz, &data[pos], 4);
        if (pos + 4 + sz > len) {
            return -1;
        }
        out.push_back(std::string((char *)&data[pos + 4], sz));
        pos += 4 + sz;
    }

    if (pos != len) {
        return -1;  // trailing garbage
    }
    return 0;
}

enum {
    RES_OK = 0,
    RES_ERR = 1,
    RES_NX = 2,
};

// The data structure for the key space.
class KeyValueStore {
public:
    HMap db;

    static uint64_t str_hash(const uint8_t *data, size_t len) {
        uint32_t h = 0x811C9DC5;
        for (size_t i = 0; i < len; i++) {
            h = (h + data[i]) * 0x01000193;
        }
        return h;
    }

    static bool entry_eq(HNode *lhs, HNode *rhs) {
        struct Entry *le = container_of(lhs, struct Entry, node);
        struct Entry *re = container_of(rhs, struct Entry, node);
        return le->key == re->key;
    }

    uint32_t get(const std::vector<std::string> &cmd, uint8_t *res, uint32_t *reslen) {
        Entry key;
        key.key = cmd[1];
        key.node.hcode = str_hash((uint8_t *)key.key.data(), key.key.size());

        HNode *node = hm_lookup(&db, &key.node, &entry_eq);
        if (!node) {
            return RES_NX;
        }

        const std::string &val = container_of(node, Entry, node)->val;
        assert(val.size() <= k_max_msg);
        memcpy(res, val.data(), val.size());
        *reslen = (uint32_t)val.size();
        return RES_OK;
    }

    uint32_t set(const std::vector<std::string> &cmd) {
        Entry key;
        key.key = cmd[1];
        key.node.hcode = str_hash((uint8_t *)key.key.data(), key.key.size());

        HNode *node = hm_lookup(&db, &key.node, &entry_eq);
        if (node) {
            container_of(node, Entry, node)->val = cmd[2];
        } else {
            Entry *ent = new Entry();
            ent->key = key.key;
            ent->node.hcode = key.node.hcode;
            ent->val = cmd[2];
            hm_insert(&db, &ent->node);
        }
        return RES_OK;
    }

    uint32_t del(const std::vector<std::string> &cmd) {
        Entry key;
        key.key = cmd[1];
        key.node.hcode = str_hash((uint8_t *)key.key.data(), key.key.size());

        HNode *node = hm_pop(&db, &key.node, &entry_eq);
        if (node) {
            delete container_of(node, Entry, node);
        }
        return RES_OK;
    }

private:
    struct Entry {
        HNode node;
        std::string key;
        std::string val;
    };
};

static KeyValueStore g_data;

static bool cmd_is(const std::string &word, const char *cmd) {
    return 0 == strcasecmp(word.c_str(), cmd);
}

static int32_t do_request(
    const uint8_t *req, uint32_t reqlen,
    uint32_t *rescode, uint8_t *res, uint32_t *reslen)
{
    std::vector<std::string> cmd;
    if (0 != parse_req(req, reqlen, cmd)) {
        msg("bad req");
        return -1;
    }
    if (cmd.size() == 2 && cmd_is(cmd[0], "get")) {
        *rescode = g_data.get(cmd, res, reslen);
    } else if (cmd.size() == 3 && cmd_is(cmd[0], "set")) {
        *rescode = g_data.set(cmd);
    } else if (cmd.size() == 2 && cmd_is(cmd[0], "del")) {
        *rescode = g_data.del(cmd);
    } else {
        // cmd is not recognized
        *rescode = RES_ERR;
        const char *msg = "Unknown cmd";
        strcpy((char *)res, msg);
        *reslen = strlen(msg);
        return 0;
    }
    return 0;
}

static bool try_one_request(ConnPtr conn) {
    // try to parse a request from the buffer
    if (conn->rbuf_size < 4) {
        // not enough data in the buffer. Will retry in the next iteration.
        return false;
    }
    uint32_t len = 0;
    memcpy(&len, &conn->rbuf[0], 4);
    if (len > k_max_msg) {
        msg("too long");
        conn->state = STATE_END;
        return false;
    }
    if (4 + len > conn->rbuf_size) {
        // not enough data in the buffer. Will retry in the next iteration.
        return false;
    }

    uint32_t rescode = 0;
    uint32_t wlen = 0;
    int32_t err = do_request(&conn->rbuf[4], len, &rescode, &conn->wbuf[4 + 4], &wlen);
    if (err) {
        conn->state = STATE_END;
        return false;
    }

    wlen += 4;
    memcpy(&conn->wbuf[0], &wlen, 4);
    memcpy(&conn->wbuf[4], &rescode, 4);
    conn->wbuf_size = 4 + wlen;

    // remove the request from the buffer.
    size_t remaining = conn->rbuf_size - 4 - len;
    if (remaining) {
        memmove(conn->rbuf, &conn->rbuf[4 + len], remaining);
    }
    conn->rbuf_size = remaining;
    conn->state = STATE_RES;
    state_res(conn);
    return (conn->state == STATE_REQ);
}

static void state_req(ConnPtr conn) {
    while (try_one_request(conn)) {}
}

static void state_res(ConnPtr conn) {
    while (conn->wbuf_sent < conn->wbuf_size) {
        ssize_t rv = send(conn->fd,
            &conn->wbuf[conn->wbuf_sent],
            conn->wbuf_size - conn->wbuf_sent, 0);
        if (rv < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                return;
            }
            msg("send() error");
            conn->state = STATE_END;
            return;
        }
        conn->wbuf_sent += (size_t)rv;
    }

    if (conn->wbuf_sent == conn->wbuf_size) {
        conn->state = STATE_REQ;
        conn->wbuf_sent = 0;
        conn->wbuf_size = 0;
        state_req(conn);
    }
}

static void connection_io(ConnPtr conn) {
    if (conn->state == STATE_REQ) {
        assert(conn->rbuf_size < sizeof(conn->rbuf));
        ssize_t rv = 0;
        do {
            rv = recv(conn->fd,
                &conn->rbuf[conn->rbuf_size],
                sizeof(conn->rbuf) - conn->rbuf_size, 0);
        } while (rv < 0 && errno == EINTR);

        if (rv < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                return;
            }
            msg("recv() error");
            conn->state = STATE_END;
            return;
        }

        if (rv == 0) {
            conn->state = STATE_END;
            return;
        }

        conn->rbuf_size += (size_t)rv;
        state_req(conn);
    } else if (conn->state == STATE_RES) {
        state_res(conn);
    } else {
        assert(0);  // not expected
    }
}

int main() {
    // Creating the socket
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        die("socket()");
    }

    // Enabling the SO_REUSEADDR option
    int val = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val)) != 0) {
        die("setsockopt()");
    }

    // Binding the socket
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(1234);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        die("bind()");
    }

    // Setting the socket to nonblocking mode
    fd_set_nb(fd);

    // Starting to listen
    if (listen(fd, SOMAXCONN) != 0) {
        die("listen()");
    }

    // Event loop
    std::vector<ConnPtr> fd2conn;
    while (true) {
        // Setting up the poll() system call
        struct pollfd pfd;
        pfd.fd = fd;
        pfd.events = POLLIN;

        std::vector<struct pollfd> poll_fds = {pfd};
        for (const auto &conn : fd2conn) {
            if (conn) {
                struct pollfd pfd;
                pfd.fd = conn->fd;
                pfd.events = (conn->state == STATE_REQ) ? POLLIN : POLLOUT;
                poll_fds.push_back(pfd);
            }
        }

        // Waiting for new events
        int rv = poll(poll_fds.data(), poll_fds.size(), 1000);
        if (rv < 0) {
            die("poll()");
        }

        // Process each event
        for (const auto &pfd : poll_fds) {
            if (pfd.revents == 0) {
                continue;
            }

            if (pfd.fd == fd) {
                // event: new connection
                if (accept_new_conn(fd2conn, fd) != 0) {
                    msg("accept_new_conn() error");
                }
            } else {
                // event: data received
                ConnPtr conn = fd2conn[pfd.fd];
                if (!conn) {
                    msg("invalid connection");
                    close(pfd.fd);
                    continue;
                }
                connection_io(conn);
                if (conn->state == STATE_END) {
                    fd2conn[conn->fd] = nullptr;
                    close(conn->fd);
                }
            }
        }
    }
    return 0;
}
