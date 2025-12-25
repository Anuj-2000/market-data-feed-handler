# Critical Thinking Questions - Answers

This document provides detailed answers to all critical thinking questions from the assignment.

## Exchange Simulator (Server)

### Q1: How do you efficiently broadcast to multiple clients without blocking?

**Answer:**

The key is using **non-blocking sockets** with **edge-triggered epoll**.

**Implementation:**
```cpp
void broadcast_message(const void* data, size_t len) {
    for (size_t i = 0; i < clients_.size(); ) {
        auto& client = clients_[i];
        
        ssize_t sent = send(client.fd, data, len, MSG_NOSIGNAL);
        
        if (sent < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // Client's send buffer full - skip this message
                ++i;
                continue;
            }
            // Other error - disconnect
            disconnect_client(i);
            // Don't increment i (next client moved to this position)
        } else {
            ++i;
        }
    }
}
```

**Key techniques:**

1. **MSG_NOSIGNAL flag**: Prevents SIGPIPE when writing to closed socket
2. **Non-blocking send()**: Returns immediately with EAGAIN if buffer full
3. **Lossy on slow clients**: Skip message instead of blocking
4. **Graceful disconnect**: Remove client on connection errors

**Why this works:**
- Fast clients get all messages
- Slow clients get most messages (acceptable for market data)
- One slow client never blocks broadcast to others
- No mutex/locks needed

**Alternative considered:** Per-client buffer queue
- **Pros**: No message loss
- **Cons**: Memory overhead, complexity, slow clients consume memory
- **Decision**: Lossy is acceptable for real-time market data

---

### Q2: What happens when a client's TCP send buffer fills up?

**Answer:**

When the send buffer (SO_SNDBUF) fills up:

**Blocking socket:**
```cpp
send(fd, data, len, 0);  // BLOCKS until buffer has space
```
❌ This would freeze the entire server!

**Non-blocking socket (our approach):**
```cpp
ssize_t n = send(fd, data, len, 0);
if (n < 0 && errno == EAGAIN) {
    // Send buffer full - can't send right now
}
```
✅ Returns immediately, we can decide what to do

**Why does buffer fill?**

1. **Network congestion**: TCP flow control, packets dropped
2. **Slow client**: Not calling recv() fast enough
3. **CPU bound client**: Processing too slow
4. **Network latency**: High RTT delays ACKs

**Our handling strategy:**

```cpp
if (errno == EAGAIN) {
    client.messages_dropped++;
    
    if (client.messages_dropped > MAX_DROPS) {
        // Client consistently slow - disconnect
        std::cerr << "Client " << client.fd << " too slow, disconnecting\n";
        disconnect_client(client);
    } else {
        // Occasional drop - skip this message
        continue;
    }
}
```

**Buffer size calculation:**
```
Message rate: 100K msg/s
Message size: 43 bytes average
Throughput: 4.3 MB/s

With 1ms network delay:
Buffer needed: 4.3 MB/s × 0.001s = 4.3 KB

We use 1MB buffer:
  1 MB / 43 bytes = 23,809 messages
  23,809 / 100,000 = 238ms buffering
```

**Production considerations:**
- Monitor drop rate per client
- Alert on high drop rates
- Consider QoS/priority for important clients
- Use larger buffers for WAN connections

---

### Q3: How do you ensure fair distribution when some clients are slower?

**Answer:**

**Definition of "fair":**
- Fast clients should never wait for slow clients
- Slow clients shouldn't starve (get some data)
- Server resources distributed proportionally

**Our fairness strategy:**

1. **Round-robin broadcast**: All clients get attempt in order
```cpp
for (auto& client : clients_) {
    send(client.fd, data, len, MSG_NOSIGNAL);
}
```

2. **Skip on EAGAIN**: Slow client doesn't block others
```cpp
if (errno == EAGAIN) {
    continue;  // Skip to next client
}
```

3. **Disconnect threshold**: Consistently slow clients removed
```cpp
if (client.drops > THRESHOLD) {
    disconnect_client(client);
}
```

**Why this is fair:**

- **Fast clients**: Always get messages (buffer never full)
- **Temporarily slow clients**: Miss some messages, but stay connected
- **Persistently slow clients**: Disconnected to free resources

**Alternative approaches:**

| Approach | Fairness | Complexity | Tradeoff |
|----------|----------|------------|----------|
| Block on slow | Equal but all suffer | Low | ❌ Fast clients penalized |
| Skip slow | Fast prioritized | Low | ⚠️ Lossy for slow |
| Per-client queue | Perfect | High | ⚠️ Memory overhead |
| Priority groups | Tiered fairness | Medium | ✅ Production option |

**Production enhancement:**
```cpp
struct ClientPriority {
    enum class Level { HIGH, MEDIUM, LOW };
    Level level;
};

// High priority: Institutional traders (never skip)
// Medium priority: Retail (skip on congestion)
// Low priority: Analytics (best effort)
```

---

### Q4: How would you handle 1000+ concurrent client connections?

**Answer:**

**Current architecture scales to ~10,000 connections:**

**Why epoll scales:**
- epoll_wait() is O(1) regardless of connection count
- Only returns ready file descriptors
- Kernel maintains efficient data structures

**Measured overhead per client:**
- Memory: 64 bytes (ClientConnection struct)
- CPU: Broadcast loop iteration (~5ns)

**For 1000 clients:**
- Memory: 64 KB (negligible)
- CPU: Broadcast overhead: 1000 × 5ns = 5μs per message
  - At 100K msg/s: 500ms/sec = 50% CPU just for iteration
  - Still feasible!

**For 10,000+ clients - optimizations needed:**

**1. Thread pool for broadcasting:**
```cpp
// Partition clients across threads
Thread 1: Clients 0-2499
Thread 2: Clients 2500-4999
Thread 3: Clients 5000-7499
Thread 4: Clients 7500-9999

// Parallel broadcast
parallel_for_each(partition, [](clients) {
    for (auto& client : clients) {
        send(client.fd, data, len, MSG_NOSIGNAL);
    }
});
```

**Benefit**: 4 threads → 4x throughput

**2. SO_REUSEPORT for multi-process:**
```cpp
// Enable SO_REUSEPORT
int reuse = 1;
setsockopt(listen_fd, SOL_SOCKET, SO_REUSEPORT, &reuse, sizeof(reuse));

// Run 4 processes on same port
// Kernel load-balances connections across processes
```

**Benefit**: 
- No shared state between processes
- Linear scaling
- Fault isolation

**3. Kernel tuning:**
```bash
# Increase connection tracking
sysctl -w net.core.somaxconn=4096

# Increase ephemeral port range
sysctl -w net.ipv4.ip_local_port_range="1024 65535"

# Increase file descriptor limit
ulimit -n 100000
```

**4. Connection pooling strategy:**
```cpp
// Group clients by activity
struct ClientTier {
    std::vector<ClientConnection> active;    // Receiving data
    std::vector<ClientConnection> idle;      // Connected but inactive
    std::vector<ClientConnection> stale;     // No activity > timeout
};

// Only broadcast to active clients
```

**5. Multicast (if network supports):**
```cpp
// One packet reaches all clients
int mcast_fd = socket(AF_INET, SOCK_DGRAM, 0);
sendto(mcast_fd, data, len, 0, &mcast_addr, sizeof(mcast_addr));
// All clients subscribed to multicast group receive
```

**Tradeoff**: UDP = no reliability guarantees

**Architecture for 100,000+ clients:**

```
                    Load Balancer (HAProxy)
                           │
        ┌──────────────────┼──────────────────┐
        ▼                  ▼                  ▼
   Server 1           Server 2           Server 3
   (30K clients)      (30K clients)      (40K clients)
        │                  │                  │
        └──────────────────┴──────────────────┘
                           │
                    Shared State (Redis)
```

**Key insight**: Vertical scaling (one machine) works to ~10K clients. Beyond that, horizontal scaling (multiple machines) is necessary.

---

## TCP Client Socket Implementation

### Q5: Why use epoll edge-triggered instead of level-triggered for feed handler?

**Answer:**

**Level-Triggered (LT) behavior:**
```
1. Data arrives on socket
2. epoll_wait() returns: "fd X is readable"
3. Read 100 bytes (but 200 bytes available)
4. epoll_wait() called again
5. IMMEDIATELY RETURNS: "fd X STILL readable"
6. Read 100 more bytes
7. epoll_wait() called again...
```

**Edge-Triggered (ET) behavior:**
```
1. Data arrives on socket
2. epoll_wait() returns: "fd X is readable"
3. Read 100 bytes (but 200 bytes available)
4. epoll_wait() called again
5. BLOCKS (won't return until NEW data arrives)
```

**Why ET is better for feed handler:**

**1. Fewer system calls:**
```cpp
// Level-triggered: N epoll_wait calls for N recv() calls
while (true) {
    epoll_wait(...);  // Returns immediately if data available
    recv(...);
}

// Edge-triggered: 1 epoll_wait, then many recv() calls
epoll_wait(...);  // Returns once
while (true) {
    ssize_t n = recv(...);
    if (n < 0 && errno == EAGAIN) break;  // Drained socket
}
```

**2. Forces good programming practices:**
- Must read until EAGAIN (complete drainage)
- Can't accidentally miss data
- Explicit about "done with this event"

**3. Better performance under load:**
```
1000 messages arrive in burst:

LT: 1000 epoll_wait calls + 1000 recv calls = 2000 syscalls
ET: 1 epoll_wait call + 1000 recv calls = 1001 syscalls

50% fewer syscalls!
```

**4. Prevents busy-looping:**
```cpp
// BAD with LT (busy loop)
while (true) {
    epoll_wait(...);  // Returns immediately
    recv(1_byte);     // Read tiny amount
    // Instant loop!
}

// ET forces proper handling
epoll_wait(...);
while (recv(...) > 0) {
    // Read until EAGAIN
}
```

**Tradeoff:**

| Aspect | Level-Triggered | Edge-Triggered |
|--------|----------------|----------------|
| Easy to use | ✅ Yes | ⚠️ More complex |
| Performance | ⚠️ More syscalls | ✅ Fewer syscalls |
| Footgun potential | ⚠️ Easy to miss data | ⚠️ Easy to miss data |
| Best for | Low-rate apps | High-rate apps |

**When to use LT:**
- Simple applications
- Low message rates (< 10K msg/s)
- Want simple code

**When to use ET (our case):**
- High message rates (100K+ msg/s)
- Performance critical
- Worth the complexity

**Our implementation:**
```cpp
struct epoll_event ev;
ev.events = EPOLLIN | EPOLLET;  // Edge-triggered
epoll_ctl(epoll_fd, EPOLL_CTL_ADD, socket_fd, &ev);

// Event loop
while (true) {
    int n = epoll_wait(epoll_fd, events, MAX_EVENTS, timeout);
    
    for (int i = 0; i < n; ++i) {
        if (events[i].events & EPOLLIN) {
            // MUST read until EAGAIN
            while (true) {
                ssize_t received = recv(fd, buffer, size, 0);
                if (received < 0) {
                    if (errno == EAGAIN || errno == EWOULDBLOCK) {
                        break;  // Drained socket
                    }
                    // Real error
                    handle_error();
                    break;
                }
                if (received == 0) {
                    // Connection closed
                    handle_disconnect();
                    break;
                }
                
                // Process received data
                parser.parse(buffer, received);
            }
        }
    }
}
```

---

### Q6: How do you handle the case where recv() returns EAGAIN/EWOULDBLOCK?

**Answer:**

**EAGAIN/EWOULDBLOCK means:** "Socket has no data available right now, but might later"

**Incorrect handling (blocking assumption):**
```cpp
while (true) {
    ssize_t n = recv(fd, buffer, size, 0);
    if (n == -1 && errno == EAGAIN) {
        std::cerr << "ERROR: No data!\n";  // WRONG!
        return;
    }
}
```

**Correct handling (expected behavior):**
```cpp
while (true) {
    ssize_t n = recv(fd, buffer, size, 0);
    
    if (n > 0) {
        // Got data - process it
        parser.parse(buffer, n);
        continue;
    }
    
    if (n == 0) {
        // Connection closed gracefully
        std::cout << "Server disconnected\n";
        return;
    }
    
    // n < 0: error occurred
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
        // NO MORE DATA AVAILABLE - this is normal!
        // Go back to epoll_wait() and wait for more
        break;
    }
    
    // Other error - real problem
    std::cerr << "recv() error: " << strerror(errno) << "\n";
    return;
}
```

**State machine view:**

```
┌──────────────┐
│ epoll_wait() │ ← Wait here until data arrives
└──────┬───────┘
       │ EPOLLIN event
       ▼
┌──────────────┐
│    recv()    │ ← Read data
└──────┬───────┘
       │
       ├─ n > 0  → Process data, loop back to recv()
       ├─ n = 0  → Connection closed
       └─ n < 0  → Check errno
                    ├─ EAGAIN → Break, go back to epoll_wait()
                    └─ Other  → Handle error
```

**Why EAGAIN is not an error:**

Non-blocking I/O is **optimistic**:
```cpp
// "Try to read, maybe there's data"
ssize_t n = recv(fd, buffer, size, 0);

// Three outcomes:
// 1. Success (n > 0): Great, we got data
// 2. Nothing available (EAGAIN): OK, we'll wait
// 3. Error (other errno): Problem, handle it
```

**Edge-triggered requires draining:**
```cpp
// With edge-triggered epoll, MUST read until EAGAIN
while (true) {
    ssize_t n = recv(...);
    if (n < 0 && errno == EAGAIN) {
        // Socket drained - done for now
        break;
    }
    // Process data
}

// Now epoll_wait() will block until NEW data arrives
```

**Common mistakes:**

❌ **Mistake 1:** Treating EAGAIN as fatal error
```cpp
if (errno == EAGAIN) {
    exit(1);  // WRONG!
}
```

❌ **Mistake 2:** Not reading until EAGAIN with ET
```cpp
// Edge-triggered
ssize_t n = recv(fd, buffer, 100, 0);  // Read 100 bytes
// But 1000 bytes available!
// epoll_wait() won't notify again - data lost!
```

❌ **Mistake 3:** Blocking on EAGAIN
```cpp
while (errno == EAGAIN) {
    sleep(1);  // WRONG - busy waiting!
    recv(...);
}
// Should use epoll_wait() instead
```

✅ **Correct pattern:**
```cpp
void read_from_socket(int fd) {
    uint8_t buffer[8192];
    
    while (true) {
        ssize_t n = recv(fd, buffer, sizeof(buffer), 0);
        
        if (n > 0) {
            parser_.parse(buffer, n);
        } else if (n == 0) {
            handle_disconnect();
            return;
        } else {  // n < 0
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // Normal - no more data right now
                return;  // Go back to epoll_wait
            } else {
                // Real error
                handle_error(errno);
                return;
            }
        }
    }
}
```

---

### Q7: What happens if the kernel receive buffer fills up?

**Answer:**

**Kernel receive buffer (SO_RCVBUF):**
- Data arrives from network → stored in kernel buffer
- Application calls recv() → moves data from kernel → userspace

**When buffer fills:**

```
Network → [Kernel Buffer: FULL] → recv() not called fast enough
```

**TCP flow control kicks in:**

1. **Receiver (us) advertises zero window:**
```
TCP Header: Window Size = 0
Meaning: "I can't accept any more data right now"
```

2. **Sender stops sending:**
```
Sender: "OK, I'll wait"
[Sender's send buffer fills up]
[Sender's send() returns EAGAIN]
```

3. **Zero Window Probe:**
```
Sender periodically asks: "Can you receive now?"
Receiver responds with current window size
```

4. **When we call recv():**
```
recv() moves data: Kernel buffer → Userspace
Kernel buffer has space now
Receiver advertises: Window Size = <available space>
Sender resumes sending
```

**Consequences:**

**1. Increased latency:**
```
Message 1: 50μs latency (normal)
Buffer fills...
Message 100: 5000μs latency (blocked by flow control)
```

**2. Sender backpressure:**
```
Our buffer full → Sender blocked → Sender's clients blocked
Cascading effect!
```

**3. No data loss:**
```
TCP guarantees delivery - data NOT dropped
But significant delays
```

**Prevention strategies:**

**1. Larger receive buffer:**
```cpp
int recvbuf = 4 * 1024 * 1024;  // 4MB
setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &recvbuf, sizeof(recvbuf));

// 4MB at 100K msg/s (4.3 MB/s):
// 4 MB / 4.3 MB/s ≈ 1 second of buffering
```

**2. Faster processing:**
```cpp
// Don't do expensive work in recv() loop
while (true) {
    ssize_t n = recv(...);
    queue.push(buffer, n);  // Fast enqueue
}

// Process in separate thread
worker_thread() {
    while (true) {
        data = queue.pop();
        expensive_processing(data);  // Can take time
    }
}
```

**3. Backpressure to application:**
```cpp
if (queue.size() > THRESHOLD) {
    std::cerr << "WARNING: Processing falling behind\n";
    // Maybe skip some processing
    // Or alert operator
}
```

**4. Monitor buffer usage:**
```cpp
// Check current buffer usage (Linux)
int bytes_available;
ioctl(fd, FIONREAD, &bytes_available);

if (bytes_available > recvbuf * 0.8) {
    std::cerr << "WARNING: Receive buffer 80% full\n";
}
```

**Our handling:**

```cpp
// Large buffer (4MB)
int recvbuf = 4 * 1024 * 1024;
setsockopt(socket_fd, SOL_SOCKET, SO_RCVBUF, &recvbuf, sizeof(recvbuf));

// Fast parser (182ns per message)
// Can process 5M msg/s - much faster than 100K arrival rate

// Result: Buffer never fills under normal load
```

**When buffer fills despite this:**
- Network faster than 100K msg/s (unexpected burst)
- Parser blocked (e.g., waiting for lock)
- CPU starvation (other processes consuming CPU)

**Recovery:**
```cpp
// Buffer fills → Flow control → Latency spikes
// But no data loss
// When recv() catches up → Normal operation resumes
```

---

### Q8: How do you detect a silent connection drop (no FIN/RST)?

**Answer:**

**Silent drop scenarios:**
1. Network cable unplugged
2. Server process killed (kill -9, no cleanup)
3. Server machine crashed
4. Firewall dropped packets
5. NAT timeout

**No FIN/RST sent** → Client doesn't know connection is dead!

**Detection methods:**

**Method 1: TCP Keepalive (Kernel-level)**

```cpp
int keepalive = 1;
setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &keepalive, sizeof(keepalive));

// Tuning parameters (Linux)
int keepidle = 60;   // Start probing after 60s of idle
int keepintvl = 10;  // Probe every 10s
int keepcnt = 3;     // Give up after 3 failed probes

setsockopt(fd, IPPROTO_TCP, TCP_KEEPIDLE, &keepidle, sizeof(keepidle));
setsockopt(fd, IPPROTO_TCP, TCP_KEEPINTVL, &keepintvl, sizeof(keepintvl));
setsockopt(fd, IPPROTO_TCP, TCP_KEEPCNT, &keepcnt, sizeof(keepcnt));
```

**Timeline:**
```
T=0s:   Last data received
T=60s:  Keepalive probe #1 sent
T=70s:  No response, probe #2 sent
T=80s:  No response, probe #3 sent
T=90s:  No response, connection declared dead
        recv() returns -1, errno = ETIMEDOUT
```

**Pros:**
- Built into kernel
- No application code needed
- Standard mechanism

**Cons:**
- Slow detection (90+ seconds)
- Not suitable for real-time systems
- OS defaults often too conservative (2+ hours!)

**Method 2: Application-Level Heartbeat (Our approach)**

**Server sends heartbeat:**
```cpp
void server_heartbeat_loop() {
    while (running_) {
        protocol::HeartbeatMessage hb;
        hb.header.msg_type = protocol::MessageType::HEARTBEAT;
        hb.header.timestamp_ns = get_timestamp_ns();
        
        broadcast_message(&hb, sizeof(hb));
        
        sleep(1);  // Heartbeat every 1 second
    }
}
```

**Client tracks heartbeat:**
```cpp
class FeedHandler {
private:
    uint64_t last_message_time_ns_;
    const uint64_t HEARTBEAT_TIMEOUT_NS = 5'000'000'000;  // 5 seconds
    
public:
    void on_message_received() {
        last_message_time_ns_ = get_timestamp_ns();
    }
    
    void check_connection() {
        uint64_t now = get_timestamp_ns();
        if (now - last_message_time_ns_ > HEARTBEAT_TIMEOUT_NS) {
            std::cerr << "No heartbeat in 5 seconds - connection lost\n";
            reconnect();
        }
    }
};
```

**Timeline:**
```
T=0s:  Last message received
T=1s:  Heartbeat received (update last_message_time)
T=2s:  Heartbeat received
T=3s:  Connection silently drops
T=3s:  No heartbeat arrives
T=4s:  Still no heartbeat
T=5s:  check_connection() detects timeout
T=5s:  reconnect() initiated
```

**Detection time: 5 seconds (much better than 90s!)**

**Pros:**
- Fast detection (configurable)
- Works at application level (aware of actual data flow)
- Can include additional info in heartbeat (sequence number, etc.)

**Cons:**
- Requires implementation
- Additional bandwidth (1 message/sec = 20 bytes/sec = negligible)

**Method 3: Write Detection**

```cpp
// Try sending data
ssize_t n = send(fd, &heartbeat, sizeof(heartbeat), MSG_NOSIGNAL);

if (n < 0) {
    if (errno == EPIPE) {
        // Connection broken
        std::cerr << "Connection broken (EPIPE)\n";
        reconnect();
    } else if (errno == ECONNRESET) {
        // Connection reset
        std::cerr << "Connection reset\n";
        reconnect();
    }
}
```

**Timeline:**
```
T=0s:  Connection silently drops
T=1s:  send() called
       TCP tries to send, no ACK
T=2s:  TCP retransmits
T=3s:  TCP retransmits again
...
T=~120s: TCP gives up, send() returns ETIMEDOUT
```

**Slow!** But eventually detects.

**Method 4: Timeout on recv()**

```cpp
struct timeval timeout;
timeout.tv_sec = 5;
timeout.tv_usec = 0;
setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

ssize_t n = recv(fd, buffer, size, 0);
if (n < 0 && errno == EAGAIN) {
    // Timeout - no data in 5 seconds
    std::cerr << "Receive timeout\n";
    reconnect();
}
```

**Tradeoff:** 
- Blocks recv() call
- Not compatible with non-blocking I/O
- Not suitable for our architecture

**Our Implementation (Hybrid):**

```cpp
// 1. Application heartbeat (fast detection)
void monitor_heartbeat() {
    if (time_since_last_message() > 5) {
        reconnect();
    }
}

// 2. TCP keepalive (backup)
enable_tcp_keepalive(fd, 30, 5, 3);
// Detects in 45 seconds if app heartbeat misses

// 3. Handle send errors
if (send() returns EPIPE) {
    reconnect();
}
```

**Best of all worlds:**
- Fast detection (5s via app heartbeat)
- Backup detection (45s via TCP keepalive)
- Immediate detection on write error

---

### Q9: Should reconnection logic be in the same thread or separate?

**Answer:**

**Option 1: Same Thread (Synchronous)**

```cpp
void main_loop() {
    while (running_) {
        if (!connected_) {
            reconnect();  // Blocks until connected or timeout
        }
        
        // Process messages
        int n = epoll_wait(...);
        for (int i = 0; i < n; ++i) {
            handle_event(events[i]);
        }
    }
}
```

**Pros:**
- Simple code
- No threading complexity
- Easy to reason about state

**Cons:**
- ❌ Blocks processing during reconnection
- ❌ If reconnect takes 30 seconds, entire app frozen
- ❌ Can't do anything else while reconnecting

**Option 2: Separate Thread (Asynchronous) - Our choice**

```cpp
// Main thread: Process messages
void main_loop() {
    while (running_) {
        if (!connected_ && !reconnect_in_progress_) {
            // Start reconnection in background
            reconnect_thread_ = std::thread(&reconnect_async, this);
            reconnect_thread_.detach();
        }
        
        if (connected_) {
            // Process messages normally
            int n = epoll_wait(...);
            handle_events(events, n);
        } else {
            // Not connected - maybe show status, wait, etc.
            std::this_thread::sleep_for(100ms);
        }
    }
}

// Separate thread: Reconnection
void reconnect_async() {
    reconnect_in_progress_ = true;
    
    for (int attempt = 1; attempt <= max_retries_; ++attempt) {
        std::cout << "Reconnection attempt " << attempt << "...\n";
        
        if (try_connect()) {
            std::cout << "Reconnected successfully!\n";
            connected_ = true;
            reconnect_in_progress_ = false;
            return;
        }
        
        // Exponential backoff
        std::this_thread::sleep_for(backoff_delay);
        backoff_delay = std::min(backoff_delay * 2, MAX_BACKOFF);
    }
    
    std::cerr << "Reconnection failed after " << max_retries_ << " attempts\n";
    reconnect_in_progress_ = false;
}
```

**Pros:**
- ✅ Main loop not blocked
- ✅ Can show UI updates during reconnection
- ✅ Can handle other events (e.g., shutdown signal)
- ✅ Exponential backoff doesn't freeze app

**Cons:**
- More complex state management
- Need to handle partial connection (DNS, TCP handshake as separate states)

**Our choice: Separate Thread**

**Rationale:**
- Feed handler should keep processing cached data during reconnection
- Visualization should stay responsive
- Exponential backoff can take minutes - unacceptable to block
- Threading is manageable with proper atomics

**Thread safety:**
```cpp
class FeedHandler {
private:
    std::atomic<bool> connected_;
    std::atomic<bool> reconnect_in_progress_;
    int socket_fd_;  // Only accessed by network thread
    std::mutex socket_mutex_;  // Protects socket operations
    
public:
    void ensure_connected() {
        if (!connected_.load() && !reconnect_in_progress_.load()) {
            std::thread(&FeedHandler::reconnect_loop, this).detach();
        }
    }
};
```

---

## Binary Protocol Parser

### Q10: How do you buffer incomplete messages across multiple recv() calls efficiently?

**Answer:**

**The problem:**

```
recv() call 1: Returns 25 bytes
  [Header: 16 bytes] [Partial payload: 9 bytes]
  Message incomplete!

recv() call 2: Returns 18 bytes
  [Remaining payload: 7 bytes] [Checksum: 4 bytes] [Next header: 7 bytes]
  Now we have complete message + partial next message
```

**Naive approach (DON'T do this):**

```cpp
std::vector<uint8_t> buffer;  // Dynamic buffer

void parse(const uint8_t* data, size_t len) {
    // Append to buffer
    buffer.insert(buffer.end(), data, data + len);
    
    // Try to parse
    while (buffer.size() >= sizeof(MessageHeader)) {
        auto* hdr = (MessageHeader*)buffer.data();
        size_t msg_size = get_message_size(hdr->msg_type);
        
        if (buffer.size() >= msg_size) {
            process_message(buffer.data());
            // Remove from buffer
            buffer.erase(buffer.begin(), buffer.begin() + msg_size);
        }
    }
}
```

**Problems:**
- `insert()` can reallocate (expensive)
- `erase()` does memmove (O(n))
- At 100K msg/s: millions of allocations/sec!

**Our approach: Fixed circular buffer**

```cpp
class MessageParser {
private:
    static constexpr size_t BUFFER_SIZE = 8192;
    uint8_t buffer_[BUFFER_SIZE];  // Fixed size, no allocation
    size_t buffer_used_;            // Bytes currently in buffer
    
public:
    size_t parse(const uint8_t* data, size_t len) {
        // 1. Copy to buffer (one memcpy)
        if (buffer_used_ + len > BUFFER_SIZE) {
            // Buffer full - shouldn't happen with proper sizing
            reset();  // Emergency reset
            return 0;
        }
        
        memcpy(buffer_ + buffer_used_, data, len);
        buffer_used_ += len;
        
        // 2. Parse complete messages
        while (buffer_used_ >= sizeof(MessageHeader)) {
            auto* hdr = (MessageHeader*)buffer_;
            size_t msg_size = get_message_size(hdr->msg_type);
            
            if (msg_size == 0 || buffer_used_ < msg_size) {
                break;  // Incomplete or invalid
            }
            
            // 3. Process message
            process_message(buffer_);
            
            // 4. Remove from buffer (one memmove)
            buffer_used_ -= msg_size;
            if (buffer_used_ > 0) {
                memmove(buffer_, buffer_ + msg_size, buffer_used_);
            }
        }
        
        return len;
    }
};
```

**Why this is efficient:**

1. **No allocations**: Fixed 8KB buffer
2. **Single memcpy** to append: O(len)
3. **Single memmove** to remove: O(remaining)
4. **Typical case**: 1-2 messages per recv(), small memmove

**Buffer sizing:**

```
Message size: 32-48 bytes
Buffer size: 8192 bytes
Capacity: ~200 messages

At 100K msg/s:
  Buffer holds 200 / 100,000 = 2ms of data
  
If processing falls behind by > 2ms:
  Buffer fills, need to handle overflow
  
Solution: 8KB is enough for 99.99% of cases
```

**Alternative: Ring buffer (zero-copy)**

```cpp
class RingBuffer {
private:
    uint8_t buffer_[8192];
    size_t write_pos_;  // Where to write next
    size_t read_pos_;   // Where to read next
    
public:
    void append(const uint8_t* data, size_t len) {
        // Write to buffer[write_pos_]
        memcpy(buffer_ + write_pos_, data, len);
        write_pos_ = (write_pos_ + len) % 8192;
    }
    
    void consume(size_t len) {
        // Just advance read pointer - no memmove!
        read_pos_ = (read_pos_ + len) % 8192;
    }
};
```

**Pros:**
- No memmove (O(1) consumption)

**Cons:**
- Wraparound logic more complex
- Can't directly cast to structs if message wraps around

**For 100K msg/s, linear buffer with memmove is fast enough:**

```
Average memmove: ~100 bytes
memmove speed: ~10 GB/s
memmove time: 100 bytes / 10 GB/s = 10 ns

Negligible compared to 182ns total parse time!
```

---

### Q11: What happens when you detect a sequence gap - drop it or request retransmission?

**Answer:**

**Sequence gap example:**

```
Received: Seq 1, 2, 3, 5, 6
Missing: Seq 4
```

**Option 1: Drop and Continue (Our approach)**

```cpp
if (header->sequence_number != last_sequence_number_ + 1) {
    uint32_t gap = header->sequence_number - last_sequence_number_ - 1;
    
    std::cerr << "Sequence gap: expected " << (last_sequence_number_ + 1)
              << ", got " << header->sequence_number
              << " (missed " << gap << " messages)\n";
    
    stats_.sequence_gaps++;
    
    // Update last sequence and continue
    last_sequence_number_ = header->sequence_number;
    
    // Process current message normally
    process_message(header);
}
```

**Pros:**
- Simple implementation
- No additional protocol complexity
- Matches UDP-like "best effort" semantics
- Appropriate for real-time data (stale data useless)

**Cons:**
- Data loss
- No recovery

**When this is appropriate:**
- **Real-time market data**: Old quote is useless
- **High message rate**: Missing 1 out of 100K is 0.001% - negligible
- **Latency-sensitive**: Retransmission adds latency

**Option 2: Request Retransmission (NACK)**

```cpp
if (sequence_gap_detected) {
    // Send NACK to server
    NACKMessage nack;
    nack.start_seq = last_sequence_number_ + 1;
    nack.end_seq = current_sequence_number - 1;
    send(socket_fd_, &nack, sizeof(nack), 0);
    
    // Wait for retransmission
    // Buffer subsequent messages until gap filled
    gap_buffer_.insert({current_sequence_number, message});
}
```

**Server handles NACK:**
```cpp
void handle_nack(const NACKMessage& nack) {
    // Retransmit requested messages
    for (uint32_t seq = nack.start_seq; seq <= nack.end_seq; ++seq) {
        if (message_cache_.contains(seq)) {
            send(client_fd, message_cache_[seq], ...);
        } else {
            // Message too old, evicted from cache
            send_error("Message no longer available");
        }
    }
}
```

**Pros:**
- No data loss (reliable delivery)
- Can recover from transient network issues

**Cons:**
- Complex protocol (NACK messages, retransmission logic)
- Server must cache messages (memory overhead)
- Adds latency (wait for retransmission)
- Out-of-order delivery (must reorder)
- Can cause cascading delays

**When this is appropriate:**
- **Critical data**: Trade confirmations, account updates
- **Low message rate**: Can afford overhead
- **Reliability > Latency**: Historical data, batch processing

**Option 3: Hybrid Approach**

```cpp
void handle_sequence_gap(uint32_t expected, uint32_t received) {
    uint32_t gap = received - expected;
    
    if (gap <= MAX_RECOVERABLE_GAP) {
        // Small gap - request retransmission
        request_nack(expected, received - 1);
        buffer_subsequent_messages();
    } else {
        // Large gap - probably network issue or server restart
        // Don't try to recover, just log and continue
        std::cerr << "Large sequence gap (" << gap << "), continuing\n";
        last_sequence_number_ = received;
        process_message_normally();
    }
}
```

**Our choice: Drop and Continue**

**Rationale:**

1. **Real-time market data is ephemeral**
   - Missing a quote from 1ms ago is irrelevant
   - Current quote supersedes it

2. **TCP provides reliability**
   - Sequence gaps are rare (only on server/client restart)
   - TCP handles packet loss/reordering

3. **Latency is critical**
   - Waiting for retransmission adds milliseconds
   - In HFT, milliseconds = money lost

4. **Simplicity**
   - No NACK protocol needed
   - No message cache needed
   - No reordering logic needed

**When we'd see sequence gaps:**

```
Scenario 1: Server restart
  Client connected to old server process
  New server process starts with seq=1
  Gap detected, client recognizes restart, resets state

Scenario 2: Client reconnect
  Client disconnects, misses messages
  Client reconnects, sees gap
  Gap is expected, continue normally

Scenario 3: TCP connection issue (rare)
  TCP provides reliability, so very rare
  If it happens, data is unrecoverable anyway
```

**Monitoring:**

```cpp
// Alert on high gap rate
if (stats_.sequence_gaps > threshold) {
    alert("High sequence gap rate: " + stats_.sequence_gaps);
    // Maybe network issue or server problem
}
```

---

### Q12: How would you handle messages arriving out of order?

**Answer:**

**First: Why would TCP messages arrive out of order?**

TCP **guarantees in-order delivery** on a single connection. So theoretically, messages can't arrive out of order.

**But in practice, out-of-order can happen:**

**Scenario 1: Multiple TCP connections**
```
Connection 1: Sends Seq 1, 3, 5 (odd messages)
Connection 2: Sends Seq 2, 4, 6 (even messages)

If connections have different latency:
  Receive order might be: 1, 3, 2, 5, 4, 6
```

**Scenario 2: Multicast UDP** (if we switched protocols)
```
UDP doesn't guarantee order
Packets can take different routes
```

**Scenario 3: Server-side parallelism**
```
Server has 4 threads generating ticks:
  Thread 1: Generates Seq 1
  Thread 2: Generates Seq 2
  Thread 3: Generates Seq 3
  Thread 4: Generates Seq 4

If Thread 2 sends before Thread 1:
  Receive order: 2, 1, 3, 4
```

**Handling strategies:**

**Strategy 1: Reorder Buffer**

```cpp
class ReorderBuffer {
private:
    std::map<uint32_t, Message> buffer_;  // seq -> message
    uint32_t next_expected_seq_;
    
public:
    void insert(uint32_t seq, const Message& msg) {
        if (seq == next_expected_seq_) {
            // In-order - process immediately
            process(msg);
            next_expected_seq_++;
            
            // Check if buffered messages are now in order
            while (buffer_.count(next_expected_seq_)) {
                process(buffer_[next_expected_seq_]);
                buffer_.erase(next_expected_seq_);
                next_expected_seq_++;
            }
        } else if (seq > next_expected_seq_) {
            // Out-of-order - buffer it
            buffer_[seq] = msg;
        } else {
            // Duplicate or very old - ignore
        }
    }
};
```

**Example:**
```
Receive: Seq 1 → Process immediately (next=2)
Receive: Seq 3 → Buffer (waiting for 2)
Receive: Seq 5 → Buffer (waiting for 2)
Receive: Seq 2 → Process 2, then process buffered 3, next=4
Receive: Seq 4 → Process 4, then process buffered 5, next=6
```

**Pros:**
- Guarantees in-order processing
- Handles any reordering

**Cons:**
- Memory overhead (buffer)
- Latency increase (wait for gaps to fill)
- Complexity

**Strategy 2: Drop Out-of-Order (Strict ordering)**

```cpp
void handle_message(uint32_t seq, const Message& msg) {
    if (seq == next_expected_seq_) {
        process(msg);
        next_expected_seq_++;
    } else {
        // Out of order - drop
        std::cerr << "Out-of-order: expected " << next_expected_seq_
                  << ", got " << seq << " - DROPPED\n";
        stats_.out_of_order_dropped++;
    }
}
```

**Pros:**
- Simple
- No memory overhead
- No latency increase

**Cons:**
- Loses data on reordering
- Only works if ordering violations rare

**Strategy 3: Process Out-of-Order (Relaxed ordering)**

```cpp
void handle_message(uint32_t seq, const Message& msg) {
    // Process regardless of order
    process(msg);
    
    // Just track that we saw this sequence
    last_sequence_seen_ = std::max(last_sequence_seen_, seq);
    
    // Maybe log if significantly out of order
    if (seq < last_sequence_seen_ - 10) {
        std::cerr << "Significantly out-of-order: " << seq << "\n";
    }
}
```

**Pros:**
- No latency
- No data loss
- Simple

**Cons:**
- Application must handle out-of-order data
- May see stale quote after newer quote

**Strategy 4: Per-Symbol Ordering**

```cpp
// Global sequence can be out of order
// But per-symbol must be in order

std::map<uint16_t, uint32_t> last_seq_per_symbol_;

void handle_message(const Message& msg) {
    uint16_t symbol = msg.header.symbol_id;
    uint32_t seq = msg.header.sequence_number;
    
    if (seq > last_seq_per_symbol_[symbol]) {
        // Newer than last message for this symbol - process
        process(msg);
        last_seq_per_symbol_[symbol] = seq;
    } else {
        // Older message for this symbol - drop
        std::cerr << "Stale message for symbol " << symbol << "\n";
    }
}
```

**Pros:**
- Ensures each symbol's data is consistent
- Handles global reordering

**Cons:**
- Still can see: Symbol 0 Seq 10, Symbol 1 Seq 5 (globally out of order)

**Our implementation: Detect but don't reorder**

```cpp
void process_message(const MessageHeader* header) {
    // Check sequence
    if (header->sequence_number != last_sequence_number_ + 1) {
        if (header->sequence_number < last_sequence_number_) {
            // Out-of-order (old message)
            std::cerr << "WARNING: Out-of-order message: "
                      << header->sequence_number << " < " 
                      << last_sequence_number_ << "\n";
            stats_.out_of_order++;
            return;  // Drop old messages
        } else {
            // Gap (missing messages)
            handle_sequence_gap();
        }
    }
    
    last_sequence_number_ = header->sequence_number;
    
    // Process normally
    cache_.update(...);
}
```

**Rationale:**

1. **TCP guarantees order** on single connection
2. **Single-threaded server** generates sequential sequences
3. **Out-of-order would indicate bug** - worth logging
4. **Drop old messages** - stale data is useless
5. **Don't buffer/reorder** - adds latency for no benefit

**When we'd reconsider:**

- Multiple TCP connections (load balancing)
- UDP multicast (no ordering guarantee)
- Multi-threaded server (parallel tick generation)

Then we'd implement reorder buffer with timeout:

```cpp
const uint64_t REORDER_TIMEOUT_NS = 1'000'000;  // 1ms

void insert_with_timeout(uint32_t seq, Message msg) {
    buffer_[seq] = {msg, get_timestamp_ns()};
    
    // Garbage collect old buffered messages
    for (auto it = buffer_.begin(); it != buffer_.end(); ) {
        if (get_timestamp_ns() - it->second.timestamp > REORDER_TIMEOUT_NS) {
            std::cerr << "Reorder timeout for seq " << it->first << "\n";
            it = buffer_.erase(it);
        } else {
            ++it;
        }
    }
}
```

---

### Q13: How do you prevent buffer overflow with malicious large message lengths?

**Answer:**

**Attack vector:**

Malicious server sends:
```cpp
MessageHeader {
    msg_type = 0x01 (TRADE)
    sequence_number = 123
    timestamp_ns = ...
    symbol_id = 42
}

// Client calculates:
size_t msg_size = get_message_size(0x01);  // Returns 32 bytes

// But header is corrupted/malicious
// Could claim to be 10GB message!
```

**Vulnerability 1: Unchecked message size**

```cpp
// VULNERABLE
size_t msg_size = get_message_size(header->msg_type);
uint8_t* buffer = malloc(msg_size);  // Could be huge!
recv(socket, buffer, msg_size, 0);    // Buffer overflow!
```

**Vulnerability 2: Integer overflow**

```cpp
// Message size from network (uint32_t)
uint32_t msg_size = ntohl(header->size_field);

// Allocate buffer
char* buffer = malloc(msg_size + 100);  // Overflow if msg_size = 0xFFFFFFFF!
```

**Vulnerability 3: Accumulated size**

```cpp
// Attacker sends many messages
while (true) {
    size_t msg_size = read_size_from_network();
    buffer.resize(buffer.size() + msg_size);  // OOM!
}
```

**Defense 1: Maximum message size**

```cpp
const size_t MAX_MESSAGE_SIZE = 1024;  // Largest valid message

size_t get_message_size(MessageType type) {
    size_t size;
    switch (type) {
        case MessageType::TRADE:
            size = TradeMessage::SIZE;  // 32 bytes
            break;
        case MessageType::QUOTE:
            size = QuoteMessage::SIZE;  // 48 bytes
            break;
        case MessageType::HEARTBEAT:
            size = HeartbeatMessage::SIZE;  // 20 bytes
            break;
        default:
            return 0;  // Invalid type
    }
    
    // Sanity check
    if (size > MAX_MESSAGE_SIZE) {
        std::cerr << "SECURITY: Message size " << size 
                  << " exceeds maximum " << MAX_MESSAGE_SIZE << "\n";
        return 0;
    }
    
    return size;
}
```

**Defense 2: Fixed-size parser buffer**

```cpp
class MessageParser {
private:
    static constexpr size_t BUFFER_SIZE = 8192;  // Fixed!
    uint8_t buffer_[BUFFER_SIZE];  // Stack allocation, not heap
    size_t buffer_used_;
    
public:
    size_t parse(const uint8_t* data, size_t len) {
        // Check overflow
        if (buffer_used_ + len > BUFFER_SIZE) {
            std::cerr << "SECURITY: Parser buffer overflow attempt\n";
            stats_.malformed_messages++;
            reset();  // Reset parser state
            return 0;  // Reject this data
        }
        
        memcpy(buffer_ + buffer_used_, data, len);
        buffer_used_ += len;
        // ...
    }
};
```

**Defense 3: Message type validation**

```cpp
bool validate_message_header(const MessageHeader* header) {
    // Check message type is valid
    if (header->msg_type != MessageType::TRADE &&
        header->msg_type != MessageType::QUOTE &&
        header->msg_type != MessageType::HEARTBEAT) {
        std::cerr << "SECURITY: Invalid message type: " 
                  << static_cast<uint16_t>(header->msg_type) << "\n";
        return false;
    }
    
    // Check symbol ID range
    if (header->symbol_id >= MAX_SYMBOLS) {
        std::cerr << "SECURITY: Symbol ID out of range: " 
                  << header->symbol_id << "\n";
        return false;
    }
    
    return true;
}
```

**Defense 4: Rate limiting**

```cpp
class RateLimiter {
private:
    uint64_t message_count_;
    uint64_t last_reset_time_;
    const uint64_t MAX_MESSAGES_PER_SECOND = 200'000;  // 2x expected
    
public:
    bool check_rate() {
        uint64_t now = get_timestamp_ns();
        
        if (now - last_reset_time_ > 1'000'000'000) {
            // Reset every second
            message_count_ = 0;
            last_reset_time_ = now;
        }
        
        message_count_++;
        
        if (message_count_ > MAX_MESSAGES_PER_SECOND) {
            std::cerr << "SECURITY: Rate limit exceeded\n";
            return false;  // Reject message
        }
        
        return true;
    }
};
```

**Defense 5: Checksum validation (detect corruption)**

```cpp
bool verify_checksum(const void* data, size_t len) {
    if (len < sizeof(uint32_t)) {
        return false;
    }
    
    size_t data_len = len - sizeof(uint32_t);
    uint32_t received_checksum;
    memcpy(&received_checksum, 
           static_cast<const uint8_t*>(data) + data_len,
           sizeof(uint32_t));
    
    uint32_t calculated = calculate_checksum(data, data_len);
    
    if (calculated != received_checksum) {
        std::cerr << "SECURITY: Checksum mismatch\n";
        return false;
    }
    
    return true;
}
```

**Defense 6: Connection-based trust**

```cpp
// Only accept connections from known IPs
bool is_trusted_source(const struct sockaddr_in& addr) {
    const char* ip = inet_ntoa(addr.sin_addr);
    
    // Whitelist of trusted IPs
    const std::vector<std::string> TRUSTED_IPS = {
        "127.0.0.1",     // Localhost
        "10.0.0.5",      // Internal server
        "192.168.1.100"  // Known exchange gateway
    };
    
    return std::find(TRUSTED_IPS.begin(), TRUSTED_IPS.end(), ip) 
           != TRUSTED_IPS.end();
}
```

**Our complete defense strategy:**

```cpp
size_t MessageParser::parse(const uint8_t* data, size_t len) {
    // 1. Rate limiting
    if (!rate_limiter_.check_rate()) {
        return 0;
    }
    
    // 2. Buffer overflow protection
    if (buffer_used_ + len > BUFFER_SIZE) {
        std::cerr << "Parser buffer overflow attempt\n";
        reset();
        return 0;
    }
    
    memcpy(buffer_ + buffer_used_, data, len);
    buffer_used_ += len;
    
    // 3. Parse with validation
    while (buffer_used_ >= sizeof(MessageHeader)) {
        auto* hdr = (MessageHeader*)buffer_;
        
        // 4. Validate message type
        if (!validate_message_header(hdr)) {
            stats_.malformed_messages++;
            // Skip this byte, try to resync
            buffer_used_--;
            memmove(buffer_, buffer_ + 1, buffer_used_);
            continue;
        }
        
        // 5. Check message size
        size_t msg_size = get_message_size(hdr->msg_type);
        if (msg_size == 0 || msg_size > MAX_MESSAGE_SIZE) {
            stats_.malformed_messages++;
            buffer_used_ -= sizeof(MessageHeader);
            memmove(buffer_, buffer_ + sizeof(MessageHeader), buffer_used_);
            continue;
        }
        
        if (buffer_used_ < msg_size) {
            break;  // Incomplete message
        }
        
        // 6. Validate checksum
        if (!verify_checksum(buffer_, msg_size)) {
            stats_.checksum_errors++;
            buffer_used_ -= msg_size;
            memmove(buffer_, buffer_ + msg_size, buffer_used_);
            continue;
        }
        
        // 7. Process message (all checks passed)
        process_message(hdr);
        
        // 8. Remove from buffer
        buffer_used_ -= msg_size;
        if (buffer_used_ > 0) {
            memmove(buffer_, buffer_ + msg_size, buffer_used_);
        }
    }
    
    return len;
}
```

**Layers of defense:**

1. Fixed-size buffer (can't overflow stack)
2. Maximum message size check
3. Message type validation
4. Symbol ID range check
5. Checksum validation
6. Rate limiting
7. Source IP validation (optional)

**Result**: Even with malicious input, worst case is:
- Parser resets (loses buffered data)
- Malformed message counter increments
- Logs security alert
- **No crash, no buffer overflow, no memory corruption**

---

## Lock-Free Symbol Cache

### Q14: How do you prevent readers from seeing inconsistent state during updates?

**Answer:**

**The problem:**

```cpp
// Writer updates multiple fields
state.bid = 100.0;    // ← Reader might read here
state.ask = 101.0;    //   and see bid=100, ask=0 (inconsistent!)
state.volume = 1000;
```

**Without synchronization:**

```
Thread 1 (Writer):          Thread 2 (Reader):
  bid = 100                   bid_copy = bid    (100)
                              ask_copy = ask    (0 - not written yet!)
  ask = 101                   
                              // Reader sees: bid=100, ask=0 - INVALID!
  volume = 1000
```

**Solution: Seqlock Pattern**

```cpp
struct AtomicMarketState {
    std::atomic<uint64_t> sequence;  // Version counter
    MarketState data;                 // Actual data
};

// Writer
void update_quote(...) {
    auto& state = states_[symbol_id];
    
    // 1. Increment sequence (make it odd)
    uint64_t seq = state.sequence.load(std::memory_order_relaxed);
    state.sequence.store(seq + 1, std::memory_order_release);
    //                                    ^^^^^^^ RELEASE
    
    // 2. Write data (readers will see odd sequence and retry)
    state.data.bid = bid;
    state.data.ask = ask;
    state.data.volume = volume;
    
    // 3. Increment sequence again (make it even)
    state.sequence.store(seq + 2, std::memory_order_release);
    //                                    ^^^^^^^ RELEASE
}

// Reader (lock-free!)
MarketState get_snapshot(uint16_t symbol_id) {
    auto& state = states_[symbol_id];
    MarketState snapshot;
    
    while (true) {
        // 1. Read sequence BEFORE data
        uint64_t seq1 = state.sequence.load(std::memory_order_acquire);
        //                                          ^^^^^^^ ACQUIRE
        
        // 2. Check if write in progress
        if (seq1 & 1) {
            // Odd sequence = writer active, retry
            continue;
        }
        
        // 3. Read data (no writer active when we started)
        snapshot = state.data;
        
        // 4. Read sequence AFTER data
        uint64_t seq2 = state.sequence.load(std::memory_order_acquire);
        //                                          ^^^^^^^ ACQUIRE
        
        // 5. Check if sequence changed
        if (seq1 == seq2) {
            // No writer intervened - consistent read!
            return snapshot;
        }
        
        // Sequence changed - writer was active, retry
    }
}
```

**Why this works:**

**Timeline 1: No concurrent write (fast path)**
```
Reader:
  T0: seq1 = sequence (=100, even)
  T1: bid_copy = bid
  T2: ask_copy = ask
  T3: volume_copy = volume
  T4: seq2 = sequence (=100, even)
  T5: seq1 == seq2? YES → Return snapshot

Result: Consistent read
```

**Timeline 2: Concurrent write detected**
```
Reader:                    Writer:
  T0: seq1 = 100 (even)
  T1: bid_copy = bid        
                            T2: sequence = 101 (odd)
  T3: ask_copy = ask        T4: bid = NEW_BID
  T5: volume_copy = volume  T6: ask = NEW_ASK
                            T7: sequence = 102 (even)
  T8: seq2 = 102 (even)
  T9: seq1 == seq2? NO (100 != 102)
  T10: RETRY from beginning

  T11: seq1 = 102 (even)
  T12: Read all data (now consistent)
  T13: seq2 = 102 (even)
  T14: seq1 == seq2? YES → Return

Result: Eventually consistent read
```