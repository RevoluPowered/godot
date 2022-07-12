#include "ipc.h"
#include <iostream>
IPCBase::IPCBase(){}
IPCBase::~IPCBase(){}

IPCClient::IPCClient(){}
IPCClient::~IPCClient(){
    close(data_socket);
}

IPCServer::IPCServer(){}
IPCServer::~IPCServer(){
    close(connection_socket);
    unlink(SOCKET_NAME);
}


// called to register the only callback for when data arrives
void IPCBase::add_receive_callback( CallbackDefinition callback )
{
    // func pointer to func pointer.
    activeCallback = callback;
}

bool IPCClient::setup()
{
    printf("Starting socket\n");
    // Hello world MacOS return point. ;)
    data_socket = socket(AF_UNIX, SOCK_SEQPACKET, 0);
    if(data_socket == -1) {
        perror("client socket");
        return false;
    }

    printf("configuring socket type and path\n");
    /* Ensure portable by resetting all to zero */
    memset(&name, 0, sizeof(name));

    /* AF_UNIX */
    name.sun_family = AF_UNIX;
    strncpy(name.sun_path, SOCKET_NAME, sizeof(name.sun_path) - 1);
    printf("waiting for connnection\n");
    int OK = connect(data_socket, (const struct sockaddr *) &name, sizeof(name));
    if (OK == -1) {
        perror("client connect");
        return false;
    }
    else
    {
        std::cout << "Connected to server" << std::endl;
    }

    printf("waiting for write of hello\n");

    char hello[] = "hello";
    OK = write(data_socket, hello, sizeof(hello));
    if(OK == -1)
    {
        perror("cant send message");
        return false;
    }

    return true;
}

bool IPCClient::poll()
{
    // unlikely to be used as (right now) later if we need a backward pipe.
    const char str[] = "Hello World from the Client!\0";
    
    int OK = write(data_socket, str, sizeof(str));
    if(OK == -1)
    {
       perror("write");
       return false; 
    }

    return true;
}

bool IPCServer::setup()
{
    unlink(SOCKET_NAME);
    printf("Setting up server connection socket\n");
    // Hello world MacOS return point. ;)
    connection_socket = socket(AF_UNIX, SOCK_SEQPACKET, 0);
    if(connection_socket == -1) {
        perror("socket");
        return false;
    }

	int flags = fcntl(connection_socket, F_GETFL );
	fcntl(connection_socket, F_SETFL, flags | O_NONBLOCK);
    /* Ensure portable by resetting all to zero */
    memset(&name, 0, sizeof(name));

    /* AF_UNIX */
    name.sun_family = AF_UNIX;

    strncpy(name.sun_path, SOCKET_NAME, sizeof(name.sun_path) - 1);

    printf("trying to bind connection\n");
    int OK = bind(connection_socket, (const struct sockaddr *) &name,sizeof(name));
    if (OK == -1) {
        perror("bind");
        return false;
    }
    printf("Starting listen logic\n");
    OK = listen(connection_socket, 32); // assume spamming of new connections
    if (OK == -1) {
        perror("listen");
        return false;
    }

    printf("started listening for new connections\n");


    return true;
}

/* We process and read the buffer once per tick */
bool IPCServer::poll()
{
    // server only
    data_socket = accept(connection_socket, NULL, NULL);
    if (data_socket == -1) {
		// we are non blocking so this can return safely.
        return false; // do nothing.
    }
    else
    {
        printf("Server accepted connection\n");
    }
    // end server only.

    int OK = read(data_socket, buffer, sizeof(buffer));
    if (OK == -1) {
        perror("server read");
        return false;
    }
    else
    {
        printf("socket read success\n");
    }

    /* Buffer must be null terminated */
    buffer[sizeof(buffer) - 1] = 0;

    /* Pass data to the application hooked in */

    std::cout << "got message" << buffer << std::endl;
    if (activeCallback) {
        activeCallback(buffer, sizeof(buffer));
    }

    /* buffer reply means we got the message */
    OK = write(data_socket, buffer, sizeof(buffer));

    buffer[sizeof(buffer) - 1] = 0;

    close(data_socket);

    return true;
}
