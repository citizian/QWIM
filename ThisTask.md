TASK_NAME

Implement full EventLoop for Reactor pattern

CONTEXT

Current project is a C++ IM server using:

Linux socket

epoll

JSON protocol

Connection class

Channel class

EventLoop skeleton

The EventLoop currently exists but is incomplete.

Goal: implement a full Reactor event loop so that Channel and Connection work correctly with epoll.

OBJECTIVE

Implement the core EventLoop logic:

epoll_create

epoll_wait loop

channel event dispatch

channel add / modify / remove

Channel must trigger callbacks for:

read events

write events

REQUIRED_FILES

Agent should update or create the following files if needed:

src/EventLoop.h
src/EventLoop.cpp
src/Channel.h
src/Channel.cpp
src/Connection.cpp
IMPLEMENTATION_REQUIREMENTS
1. EventLoop class

Responsibilities:

manage epoll instance

register Channel

update Channel events

remove Channel

run event loop

Example interface:

class EventLoop {
public:
    EventLoop();
    ~EventLoop();

    void loop();

    void updateChannel(Channel* channel);
    void removeChannel(Channel* channel);

private:
    int epollfd;
    std::vector<epoll_event> events;
};
2. EventLoop::loop()

Implement main epoll loop:

Pseudo logic:

while (true):

    n = epoll_wait(epollfd)

    for each event:

        channel = event.data.ptr

        channel.handleEvent()
3. updateChannel()

Should support:

EPOLL_CTL_ADD
EPOLL_CTL_MOD

Use epoll_ctl.

Each Channel should store its fd and events.

4. removeChannel()

Remove channel from epoll:

epoll_ctl(EPOLL_CTL_DEL)
5. Channel class

Channel must contain:

fd

interested events

read callback

write callback

Example interface:

class Channel {
public:
    int fd;
    uint32_t events;

    std::function<void()> readCallback;
    std::function<void()> writeCallback;

    void handleEvent();
};
6. Channel::handleEvent()

Behavior:

if event has EPOLLIN:
    call readCallback()

if event has EPOLLOUT:
    call writeCallback()
7. Connection integration

Connection should:

create a Channel

set readCallback → Connection::handleRead

set writeCallback → Connection::handleWrite

register channel to EventLoop

ACCEPTANCE_CRITERIA

Task is complete when:

- EventLoop successfully runs epoll_wait loop
- Channel callbacks are triggered correctly
- Connection read/write works
- IM features still work:
    login
    broadcast
    private message
TEST_PLAN

Manual testing:

Start server

Start multiple clients

Test:

login
group chat
private chat
online list
heartbeat

Expected:

events handled correctly

no crash

no fd leak

ESTIMATED_WORK
time: 3–5 hours
difficulty: medium
expected LOC: ~200
