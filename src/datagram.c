/* The MIT License

Copyright (c) 2010 Dylan Smith

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.

TNFS daemon datagram handler

*/

#include <sys/types.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <signal.h>

#ifdef UNIX
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/select.h>
#endif

#ifdef WIN32
#include <windows.h>
#endif

#include "tnfs.h"
#include "datagram.h"
#include "log.h"
#include "endian.h"
#include "session.h"
#include "errortable.h"
#include "directory.h"
#include "tnfs_file.h"

int sockfd;		 /* UDP global socket file descriptor */
int tcplistenfd; /* TCP listening socket file descriptor */

tnfs_cmdfunc dircmd[NUM_DIRCMDS] =
	{&tnfs_opendir, &tnfs_readdir, &tnfs_closedir,
	 &tnfs_mkdir, &tnfs_rmdir, &tnfs_telldir, &tnfs_seekdir,
	 &tnfs_opendirx, &tnfs_readdirx};

tnfs_cmdfunc filecmd[NUM_FILECMDS] =
	{&tnfs_open_deprecated, &tnfs_read, &tnfs_write, &tnfs_close,
	 &tnfs_stat, &tnfs_lseek, &tnfs_unlink, &tnfs_chmod, &tnfs_rename,
	 &tnfs_open};

const char *sesscmd_names[NUM_SESSCMDS] =
	{
		"TNFS_MOUNT",
		"TNFS_UMOUNT"};

const char *dircmd_names[NUM_DIRCMDS] =
	{"TNFS_OPENDIR",
	 "TNFS_READDIR",
	 "TNFS_CLOSEDIR",
	 "TNFS_MKDIR",
	 "TNFS_RMDIR",
	 "TNFS_TELLDIR",
	 "TNFS_SEEKDIR",
	 "TNFS_OPENDIRX",
	 "TNFS_READDIRX"};

const char *filecmd_names[NUM_FILECMDS] =
	{
		"TNFS_OPENFILE_OLD",
		"TNFS_READ",
		"TNFS_WRITE",
		"TNFS_CLOSE",
		"TNFS_STAT",
		"TNFS_SEEK",
		"TNFS_UNLINK",
		"TNFS_CHMOD",
		"TNFS_RENAME",
		"TNFS_OPEN"};

const char *get_cmd_name(uint8_t cmd)
{
	uint8_t class = cmd & 0xF0;
	uint8_t index = cmd & 0x0F;

	if(class == CLASS_FILE)
	{
		if(index < NUM_FILECMDS)
			return filecmd_names[index];

	}
	else if(class == CLASS_DIRECTORY)
	{
		if(index < NUM_DIRCMDS)
			return dircmd_names[index];

	}
	else if(class == CLASS_SESSION)
	{
		if(index < NUM_SESSCMDS)
			return sesscmd_names[index];
	}

	return "UNKNOWN_CMD";
}

void tnfs_sockinit()
{
	struct sockaddr_in servaddr;

#ifdef WIN32
	WSADATA wsaData;
	if (WSAStartup(MAKEWORD(2, 0), &wsaData) != 0)
		die("WSAStartup() failed");
#endif

	/* Create the UDP socket */
	sockfd = socket(AF_INET, SOCK_DGRAM, 0);
	if (sockfd < 0)
		die("Unable to open socket");

	/* set up the network */
	memset(&servaddr, 0, sizeof(servaddr));
	servaddr.sin_family = AF_INET;
	servaddr.sin_addr.s_addr = htons(INADDR_ANY);
	servaddr.sin_port = htons(TNFSD_PORT);

	if (bind(sockfd, (struct sockaddr *)&servaddr, sizeof(servaddr)) < 0)
		die("Unable to bind");

	/* Create the TCP socket */
	tcplistenfd = socket(AF_INET, SOCK_STREAM, 0);
	if (tcplistenfd < 0)
	{
		die("Unable to create TCP socket");
	}
	if (setsockopt(tcplistenfd, SOL_SOCKET, SO_REUSEADDR, &(int){1}, sizeof(int)) < 0)
	{
    	die("setsockopt(SO_REUSEADDR) failed");
	}
	signal(SIGPIPE, SIG_IGN);

	memset(&servaddr, 0, sizeof(servaddr));
	servaddr.sin_family = AF_INET;
	servaddr.sin_addr.s_addr = htons(INADDR_ANY);
	servaddr.sin_port = htons(TNFSD_PORT);
	if (bind(tcplistenfd, (struct sockaddr *)&servaddr,
			 sizeof(servaddr)) < 0)
	{
		die("Unable to bind TCP socket");
	}
	listen(tcplistenfd, 5);
}

void tnfs_mainloop()
{
	int readyfds, i;
	fd_set fdset;
	fd_set errfdset;
	TcpConnection tcpsocks[MAX_TCP_CONN];

	memset(&tcpsocks, 0, sizeof(tcpsocks));

	while (1)
	{
		FD_ZERO(&fdset);

		/* add UDP socket and TCP listen socket to fdset */
		FD_SET(sockfd, &fdset);
		FD_SET(tcplistenfd, &fdset);

		for (i = 0; i < MAX_TCP_CONN; i++)
		{
			if (tcpsocks[i].cli_fd)
			{
				FD_SET(tcpsocks[i].cli_fd, &fdset);
			}
		}

		FD_COPY(&fdset, &errfdset);
		if ((readyfds = select(FD_SETSIZE, &fdset, NULL, &errfdset, NULL)) != 0)
		{
			if (readyfds < 0)
			{
				die("select() failed\n");
			}

			/* handle fds ready for reading */
			/* UDP message? */
			if (FD_ISSET(sockfd, &fdset))
			{
				tnfs_handle_udpmsg();
			}
			/* Incoming TCP connection? */
			else if (FD_ISSET(tcplistenfd, &fdset))
			{
				tcp_accept(&tcpsocks[0]);
			}
			else
			{
				for (i = 0; i < MAX_TCP_CONN; i++)
				{
					if (tcpsocks[i].cli_fd)
					{
						if (FD_ISSET(tcpsocks[i].cli_fd, &fdset))
						{
							tnfs_handle_tcpmsg(&tcpsocks[i]);
						}
					}
				}
			}
		}
	}
}

void tcp_accept(TcpConnection *tcp_conn_list)
{
	int acc_fd, i;
	struct sockaddr_in cliaddr;
	socklen_t cli_len = sizeof(cliaddr);
	TcpConnection *tcp_conn;

	acc_fd = accept(tcplistenfd, (struct sockaddr *)&cliaddr, &cli_len);
	if (acc_fd < 1)
	{
		fprintf(stderr, "WARNING: unable to accept TCP connection: %s\n", strerror(errno));
		return;
	}

	tcp_conn = tcp_conn_list;
	for (i = 0; i < MAX_TCP_CONN; i++)
	{
		if (tcp_conn->cli_fd == 0)
		{
			MSGLOG(cliaddr.sin_addr.s_addr, "New TCP connection at index %d.", i);
			tcp_conn->cli_fd = acc_fd;
			tcp_conn->cliaddr = cliaddr;
			return;
		}
		tcp_conn++;
	}

	MSGLOG(cliaddr.sin_addr.s_addr, "Can't accept client; too many connections.");

	/* tell the client 'too many connections' */
	unsigned char txbuf[9];
	uint16tnfs(txbuf, 0); // connID
	*(txbuf + 2) = 0; 	  // retry
	*(txbuf + 3) = 0;     // command
	*(txbuf + 4) = 0xFF;  // error
	*(txbuf + 5) = PROTOVERSION_LSB;
	*(txbuf + 6) = PROTOVERSION_MSB;

	write(acc_fd, txbuf, sizeof(txbuf));
	close(acc_fd);

}

void tnfs_handle_udpmsg()
{
	socklen_t len;
	int rxbytes;
	struct sockaddr_in cliaddr;
	unsigned char rxbuf[MAXMSGSZ];

	len = sizeof(cliaddr);
	rxbytes = recvfrom(sockfd, (char *)rxbuf, sizeof(rxbuf), 0,
					   (struct sockaddr *)&cliaddr, &len);

	if (rxbytes >= TNFS_HEADERSZ)
	{
		/* probably a valid TNFS packet, decode it */
		tnfs_decode(&cliaddr, 0, rxbytes, rxbuf);
	}
	else
	{
		MSGLOG(cliaddr.sin_addr.s_addr,
			   "Invalid datagram received");
	}

	*(rxbuf + rxbytes) = 0;
}

void tnfs_handle_tcpmsg(TcpConnection *tcp_conn)
{
	unsigned char buf[MAXMSGSZ];
	int sz;

	sz = read(tcp_conn->cli_fd, buf, sizeof(buf));
	if (sz == 0) {
		MSGLOG(tcp_conn->cliaddr.sin_addr.s_addr, "Disconnected client.");
		tnfs_removesession_by_cli_fd(tcp_conn->cli_fd);
		tcp_conn->cli_fd = 0;
		return;
	}
	tnfs_decode(&tcp_conn->cliaddr, tcp_conn->cli_fd, sz, buf);
}

void tnfs_decode(struct sockaddr_in *cliaddr, int cli_fd, int rxbytes, unsigned char *rxbuf)
{
	Header hdr;
	Session *sess;
	int sindex;
	int datasz = rxbytes - TNFS_HEADERSZ;
	int cmdclass, cmdidx;
	unsigned char *databuf = rxbuf + TNFS_HEADERSZ;

	memset(&hdr, 0, sizeof(hdr));

	/* note: don't forget about byte alignment issues on some
	 * architectures... */
	hdr.sid = tnfs16uint(rxbuf);
	hdr.seqno = *(rxbuf + 2);
	hdr.cmd = *(rxbuf + 3);
	hdr.ipaddr = cliaddr->sin_addr.s_addr;
	hdr.port = ntohs(cliaddr->sin_port);
	hdr.cli_fd = cli_fd;

#ifdef DEBUG
	TNFSMSGLOG(&hdr, "REQUEST cmd=0x%02x %s", hdr.cmd, get_cmd_name(hdr.cmd));
#endif

	/* The MOUNT command is the only one that doesn't need an
	 * established session (since MOUNT is actually what will
	 * establish the session) */
	if (hdr.cmd != TNFS_MOUNT)
	{
		sess = tnfs_findsession_sid(hdr.sid, &sindex);
		if (sess == NULL)
		{
			tnfs_invalidsession(&hdr);
			return;
		}
		if (sess->ipaddr != hdr.ipaddr)
		{
			TNFSMSGLOG(&hdr, "Session and IP do not match");
			return;
		}
		/* Update session timestamp */
		sess->last_contact = time(NULL);
	}
	else
	{
		tnfs_mount(&hdr, databuf, datasz);
		return;
	}

	/* client is asking for a resend */
	if (hdr.seqno == sess->lastseqno)
	{
		tnfs_resend(sess, cliaddr, cli_fd);
		return;
	}

	/* find the command class and pass it off to the right
	 * function */
	cmdclass = hdr.cmd & 0xF0;
	cmdidx = hdr.cmd & 0x0F;
	switch (cmdclass)
	{
	case CLASS_SESSION:
		switch (cmdidx)
		{
		case TNFS_UMOUNT:
			tnfs_umount(&hdr, sess, sindex);
			break;
		default:
			tnfs_badcommand(&hdr, sess);
		}
		break;
	case CLASS_DIRECTORY:
		if (cmdidx < NUM_DIRCMDS)
			(*dircmd[cmdidx])(&hdr, sess, databuf, datasz);
		else
			tnfs_badcommand(&hdr, sess);
		break;
	case CLASS_FILE:
		if (cmdidx < NUM_FILECMDS)
			(*filecmd[cmdidx])(&hdr, sess, databuf, datasz);
		else
			tnfs_badcommand(&hdr, sess);
		break;
	default:
		tnfs_badcommand(&hdr, sess);
	}
}

void tnfs_invalidsession(Header *hdr)
{
	TNFSMSGLOG(hdr, "Invalid session ID");
	hdr->status = TNFS_EBADSESSION;
	tnfs_send(NULL, hdr, NULL, 0);
}

void tnfs_badcommand(Header *hdr, Session *sess)
{
	TNFSMSGLOG(hdr, "Bad command");
	hdr->status = TNFS_ENOSYS;
	tnfs_send(sess, hdr, NULL, 0);
}

void tnfs_send(Session *sess, Header *hdr, unsigned char *msg, int msgsz)
{
	struct sockaddr_in cliaddr;
	ssize_t txbytes;
	unsigned char txbuf_nosess[5];
	unsigned char *txbuf = sess ? sess->lastmsg : txbuf_nosess;

	// TNFS_HEADERSZ + statuscode + msg
	if (TNFS_HEADERSZ + 1 + msgsz > MAXMSGSZ)
	{
		die("tnfs_send: Message too big");
	}

	cliaddr.sin_family = AF_INET;
	cliaddr.sin_addr.s_addr = hdr->ipaddr;
	cliaddr.sin_port = htons(hdr->port);

	uint16tnfs(txbuf, sess ? hdr->sid : 0);
	*(txbuf + 2) = hdr->seqno;
	*(txbuf + 3) = hdr->cmd;
	*(txbuf + 4) = hdr->status;
	if (msg)
		memcpy(txbuf + 5, msg, msgsz);

	if (sess)
	{
		sess->lastmsgsz = TNFS_HEADERSZ + 1 + msgsz; /* header + status code + payload */
		sess->lastseqno = hdr->seqno;
	}

	if (hdr->cli_fd == 0)
	{
		txbytes = sendto(sockfd, WIN32_CHAR_P txbuf, msgsz + TNFS_HEADERSZ + 1, 0,
						 (struct sockaddr *)&cliaddr, sizeof(cliaddr));
	}
	else
	{
        txbytes = write(hdr->cli_fd, WIN32_CHAR_P txbuf, msgsz + TNFS_HEADERSZ + 1); 
	}

	if (txbytes < TNFS_HEADERSZ + 1 + msgsz)
	{
		TNFSMSGLOG(hdr, "Message was truncated");
	}
}

void tnfs_resend(Session *sess, struct sockaddr_in *cliaddr, int cli_fd)
{
	int txbytes;
	if (cli_fd == 0)
	{
		txbytes = sendto(sockfd, WIN32_CHAR_P sess->lastmsg, sess->lastmsgsz, 0,
						(struct sockaddr *)cliaddr, sizeof(struct sockaddr_in));
	}
	else
	{
		txbytes = write(cli_fd, WIN32_CHAR_P sess->lastmsg, sess->lastmsgsz); 
	}
	if (txbytes < sess->lastmsgsz)
	{
		MSGLOG(cliaddr->sin_addr.s_addr,
			   "Retransmit was truncated");
	}
}
