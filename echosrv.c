/*
 *  libevent echo server example.
 *   
 */
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include <arpa/inet.h>

#include <sys/time.h>

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <err.h>

#include <event.h>

#define SERVER_PORT 5555

/**
 * A struct for client specific data, in this simple case the only 
 * client specific data is the read event.
 */
struct client {
	struct event ev_read;
};
/* *
 * Set a socket to non-blocking mode.
 * */
int setnonblock(int fd)
{
	int flags;
	flags = fcntl(fd, F_GETFL);
	if (flags < 0)
		return flags;
	flags |= O_NONBLOCK;
	if (fcntl(fd, F_SETFL, flags) < 0)
		return -1;
	return 0;
}

/* *
 * This function will be called by libevent when the client socket is 
 * ready for reading */
void on_read(int fd, short ev, void *arg)
{
	struct client *client = (struct client *)arg;
	u_char buf[8196];
	int len, wlen;

	len = read(fd, buf, sizeof(buf));
	if (len == 0) {
		/* Client disconnected, remove the read event and the 
		 * free the client structure. */
		printf("Client disconnected.\n");
		close(fd);
		event_del(&client->ev_read);
		free(client);
		return;
	} else if (len < 0) {
		/* some other error occurred, close the socket, remove
		 * the event and free the client structure. */
		printf("Sockent failure, disconnecting client: %s",
				strerror(errno));
		close(fd);
		event_del(&client->ev_read);
		free(client);
		return;
	}
	/* XXX For the sake of simplicity we'll echo the data write
	 * back to the client. Normally we shouldn't do this in a
	 * non-blocking app, we should queue the data and wait to be
	 * told that we can write.
	 */
	wlen = write(fd, buf, len);
	if (wlen < len) {
		/* We didn't write all our data. If we had proper
		 * queueing/buffering setup, we'd finish off the write 
		 * when told we can write again. For this simple case
		 * we'll just lose the data that didn't make it in the
		 * write */
		printf("Short write, not all data echoed back to client.\n");
	}
}

/**
 * This function will be called by libevent when there is a connection
 * ready to be accepted
 */
void on_accept(int fd, short ev, void *arg)
{
	int client_fd;
	struct sockaddr_in client_addr;
	socklen_t client_len = sizeof(client_addr);
	struct client *client;
	/* Accept the new connection. */
	client_fd = accept(fd, (struct sockaddr *)&client_addr, &client_len);
	if (client_fd == -1) {
		warn("accept failed");
		return ;
	}
	/* Set the client socket to non-blocking mode. */
	if (setnonblock(client_fd) < 0)
		warn("failed to set client socket non-blocking");
	/* We've accepted a new client, allocate a client object to 
	 * maintain the state of this client. */
	client = calloc(1, sizeof(*client));
	if (client == NULL)
		err(1, "malloc failed");

	/* Setup the read event, libevnet will call on_read() whenever
	 * the clients socket becomes read ready. We also make the
	 * read events sockets becomes read ready. We also make the 
	 * read event persistent so we didn't have to re-add after each
	 * read. */
	event_set(&client->ev_read, client_fd, EV_READ|EV_PERSIST, on_read,
			client);
	event_add(&client->ev_read, NULL);
	printf("Accepted connection from %s\n",
			inet_ntoa(client_addr.sin_addr));
}

int main(int argc, char **argv)
{
	int listen_fd;
	struct sockaddr_in listen_addr;
	int reuseaddr_on = 1;

	/* The socket accept event */
	struct event ev_accept;

	/* Initialize libevent */
	event_init();

	/* Create our listening socket. This is largely boiler plate
	 * code that I'll abstract away in the future. */
	listen_fd = socket(AF_INET, SOCK_STREAM, 0);
	if (listen_fd < 0)
		err(1, "listen failed");
	if (setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &reuseaddr_on, 
				sizeof(reuseaddr_on)) == -1)
		err(1, "setsockaddr_on failed");
	memset(&listen_addr, 0, sizeof(listen_addr));
	listen_addr.sin_family = AF_INET;
	listen_addr.sin_addr.s_addr = INADDR_ANY;
	listen_addr.sin_port = htons(SERVER_PORT);
	if (bind(listen_fd, (struct sockaddr *)&listen_addr,
				sizeof(listen_addr)) < 0)
		err(1, "bind failed");
	if (listen(listen_fd, 5) < 0)
		err(1, "listen failed");

	/* Set the socket to non-blocking,this is essential in event
	 * based programming with libevent. */
	if (setnonblock(listen_fd) < 0)
		err(1, "failed to set server socket to non-blocking");

	/* We now have a listening socket, we create a read event to 
	 * be notified when a client connects. */
	event_set(&ev_accept, listen_fd, EV_READ|EV_PERSIST, on_accept, NULL);
	event_add(&ev_accept, NULL);

	/* Start the libevent event loop. */
	event_dispatch();

	return 0;
}
