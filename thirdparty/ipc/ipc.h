#ifndef AF_UNIX_IPC_INCLUDE
#define AF_UNIX_IPC_INCLUDE

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <fcntl.h>
#include <unistd.h>


/* Inter process communication system 
 * - Platform agnostic using AF_UNIX sockets
 *   On Mac we must use DGRAM mode.
 *   We'd like to make this connectionless would be easier code wise.
 */

using CallbackDefinition = void (*)(const char * /* string data received */, int /*strlen */);
#define BufferSize 256
#define SOCKET_NAME "/tmp/som74yhe.socket"

class IPCBase 
{
    protected:
    CallbackDefinition activeCallback = nullptr;
    struct sockaddr_un name;
    // last char is always null, preventing reading more than buffer size.
    char buffer[BufferSize] = {0};
    int data_socket = -1;
    public:
    IPCBase();
    virtual ~IPCBase();
	virtual bool setup() = 0; // setup is always different
    virtual bool poll() = 0;
	virtual void add_receive_callback( CallbackDefinition callback );
};

class IPCClient : public IPCBase
{
	public:
	IPCClient();
	virtual ~IPCClient();
    bool setup();
	bool setup_one_shot( const char *str, int n );
    bool poll();
	void send_message( const char * str, int n /* length */);
};

class IPCServer : public IPCBase
{
    // server has more than one handle here
    int connection_socket = -1;
    public:
    IPCServer();
    virtual ~IPCServer();
    bool setup();
    bool poll();
};

#endif // AF_UNIX_IPC_INCLUDE