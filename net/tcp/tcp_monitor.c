/****************************************************************************
 * net/tcp/tcp_monitor.c
 *
 * Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.  The
 * ASF licenses this file to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance with the
 * License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  See the
 * License for the specific language governing permissions and limitations
 * under the License.
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdint.h>
#include <assert.h>
#include <debug.h>

#include <nuttx/net/tcp.h>

#include "devif/devif.h"
#include "socket/socket.h"
#include "tcp/tcp.h"

#ifdef NET_TCP_HAVE_STACK

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static void tcp_close_connection(FAR struct socket *psock, uint16_t flags);
static uint16_t tcp_monitor_event(FAR struct net_driver_s *dev,
                                  FAR void *pvconn, FAR void *pvpriv,
                                  uint16_t flags);

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: tcp_close_connection
 *
 * Description:
 *   Called when a loss-of-connection event has occurred.
 *
 * Input Parameters:
 *   psock    The TCP socket structure associated.
 *   flags    Set of connection events events
 *
 * Returned Value:
 *   None
 *
 * Assumptions:
 *   The caller holds the network lock.
 *
 ****************************************************************************/

static void tcp_close_connection(FAR struct socket *psock, uint16_t flags)
{
  /* These loss-of-connection events may be reported:
   *
   *   TCP_CLOSE: The remote host has closed the connection
   *   TCP_ABORT: The remote host has aborted the connection
   *   TCP_TIMEDOUT: Connection aborted due to too many retransmissions.
   *   NETDEV_DOWN: The network device went down
   *
   * And we need to set these two socket status bits appropriately:
   *
   *  _SF_CONNECTED==1 && _SF_CLOSED==0 - the socket is connected
   *  _SF_CONNECTED==0 && _SF_CLOSED==1 - the socket was gracefully
   *                                       disconnected
   *  _SF_CONNECTED==0 && _SF_CLOSED==0 - the socket was rudely disconnected
   */

  if ((flags & TCP_CLOSE) != 0)
    {
      /* The peer gracefully closed the connection.  Marking the
       * connection as disconnected will suppress some subsequent
       * ENOTCONN errors from receive.  A graceful disconnection is
       * not handle as an error but as an "end-of-file"
       */

      psock->s_flags &= ~_SF_CONNECTED;
      psock->s_flags |= _SF_CLOSED;
    }
  else if ((flags & (TCP_ABORT | TCP_TIMEDOUT | NETDEV_DOWN)) != 0)
    {
      /* The loss of connection was less than graceful.  This will
       * (eventually) be reported as an ENOTCONN error.
       */

      psock->s_flags &= ~(_SF_CONNECTED | _SF_CLOSED);
    }
}

/****************************************************************************
 * Name: tcp_monitor_event
 *
 * Description:
 *   Some connection related event has occurred
 *
 * Input Parameters:
 *   dev      The device which as active when the event was detected.
 *   conn     The connection structure associated with the socket
 *   flags    Set of events describing why the callback was invoked
 *
 * Returned Value:
 *   None
 *
 * Assumptions:
 *   The network is locked.
 *
 ****************************************************************************/

static uint16_t tcp_monitor_event(FAR struct net_driver_s *dev,
                                  FAR void *pvconn, FAR void *pvpriv,
                                  uint16_t flags)
{
  FAR struct socket *psock = (FAR struct socket *)pvpriv;

  if (psock != NULL)
    {
      ninfo("flags: %04x s_flags: %02x\n", flags, psock->s_flags);

      /* TCP_DISCONN_EVENTS: TCP_CLOSE, TCP_ABORT, TCP_TIMEDOUT, or
       * NETDEV_DOWN.  All loss-of-connection events.
       */

      if ((flags & TCP_DISCONN_EVENTS) != 0)
        {
          tcp_close_connection(psock, flags);
        }

      /* TCP_CONNECTED: The socket is successfully connected */

      else if ((flags & TCP_CONNECTED) != 0)
        {
#if 0 /* REVISIT: Assertion fires.  Why? */
          FAR struct tcp_conn_s *conn =
            (FAR struct tcp_conn_s *)psock->s_conn;

          /* Make sure that this is the device bound to the connection */

          DEBUGASSERT(conn->dev == NULL || conn->dev == dev);
          conn->dev = dev;
#endif

          /* If there is no local address assigned to the socket (perhaps
           * because it was INADDR_ANY), then assign it the address of the
           * connecting device.
           *
           * TODO: Implement this.
           */

          /* Clear the socket error */

          _SO_SETERRNO(psock, OK);

          /* Indicate that the socket is now connected */

          psock->s_flags |= (_SF_BOUND | _SF_CONNECTED);
          psock->s_flags &= ~_SF_CLOSED;
        }
    }

  return flags;
}

/****************************************************************************
 * Name: tcp_shutdown_monitor
 *
 * Description:
 *   Stop monitoring TCP connection changes for a given socket.
 *
 * Input Parameters:
 *   conn  - The TCP connection of interest
 *   flags - Indicates the type of shutdown.  TCP_CLOSE or TCP_ABORT
 *
 * Returned Value:
 *   None
 *
 * Assumptions:
 *   The caller holds the network lock (if not, it will be locked momentarily
 *   by this function).
 *
 ****************************************************************************/

static void tcp_shutdown_monitor(FAR struct tcp_conn_s *conn, uint16_t flags)
{
  DEBUGASSERT(conn);

  /* Perform callbacks to assure that all sockets, including dup'ed copies,
   * are informed of the loss of connection event.
   */

  net_lock();
  tcp_callback(conn->dev, conn, flags);

  /* Free all allocated connection event callback structures */

  while (conn->connevents != NULL)
    {
      devif_conn_callback_free(conn->dev, conn->connevents,
                               &conn->connevents);
    }

  net_unlock();
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: tcp_start_monitor
 *
 * Description:
 *   Set up to receive TCP connection state changes for a given socket
 *
 * Input Parameters:
 *   psock - The socket of interest
 *
 * Returned Value:
 *   On success, tcp_start_monitor returns OK; On any failure,
 *   tcp_start_monitor will return a negated errno value.  The only failure
 *   that can occur is if the socket has already been closed and, in this
 *   case, -ENOTCONN is returned.
 *
 * Assumptions:
 *   The caller holds the network lock (if not, it will be locked momentarily
 *   by this function).
 *
 ****************************************************************************/

int tcp_start_monitor(FAR struct socket *psock)
{
  FAR struct devif_callback_s *cb;
  FAR struct tcp_conn_s *conn;
  bool nonblock_conn;

  DEBUGASSERT(psock != NULL && psock->s_conn != NULL);
  conn = (FAR struct tcp_conn_s *)psock->s_conn;

  net_lock();

  /* Non-blocking connection ? */

  nonblock_conn = (conn->tcpstateflags == TCP_SYN_SENT &&
                   _SS_ISNONBLOCK(psock->s_flags));

  /* Check if the connection has already been closed before any callbacks
   * have been registered. (Maybe the connection is lost before accept has
   * registered the monitoring callback.)
   */

  if (!(conn->tcpstateflags == TCP_ESTABLISHED ||
        conn->tcpstateflags == TCP_SYN_RCVD || nonblock_conn))
    {
      /* Invoke the TCP_CLOSE connection event now */

      tcp_shutdown_monitor(conn, TCP_CLOSE);

      /* And return -ENOTCONN to indicate the monitor was not started
       * because the socket was already disconnected.
       */

      net_unlock();
      return -ENOTCONN;
    }

  /* Allocate a callback structure that we will use to get callbacks if
   * the network goes down.
   */

  cb = devif_callback_alloc(conn->dev, &conn->connevents);
  if (cb != NULL)
    {
      cb->event = tcp_monitor_event;
      cb->priv  = (FAR void *)psock;
      cb->flags = TCP_DISCONN_EVENTS;

      /* Monitor the connected event */

      if (nonblock_conn)
        {
          cb->flags |= TCP_CONNECTED;
        }
    }

  net_unlock();
  return OK;
}

/****************************************************************************
 * Name: tcp_stop_monitor
 *
 * Description:
 *   Stop monitoring TCP connection changes for a sockets associated with
 *   a given TCP connection structure.
 *
 * Input Parameters:
 *   conn - The TCP connection of interest
 *   flags    Set of disconnection events
 *
 * Returned Value:
 *   None
 *
 * Assumptions:
 *   The caller holds the network lock (if not, it will be locked momentarily
 *   by this function).
 *
 ****************************************************************************/

void tcp_stop_monitor(FAR struct tcp_conn_s *conn, uint16_t flags)
{
  DEBUGASSERT(conn != NULL);

  /* Stop the network monitor */

  tcp_shutdown_monitor(conn, flags);
}

/****************************************************************************
 * Name: tcp_close_monitor
 *
 * Description:
 *   One socket in a group of dup'ed sockets has been closed.  We need to
 *   selectively terminate just those things that are waiting of events
 *   from this specific socket.  And also recover any resources that are
 *   committed to monitoring this socket.
 *
 * Input Parameters:
 *   psock - The TCP socket structure that is closed
 *
 * Returned Value:
 *   None
 *
 * Assumptions:
 *   The caller holds the network lock (if not, it will be locked momentarily
 *   by this function).
 *
 ****************************************************************************/

void tcp_close_monitor(FAR struct socket *psock)
{
  FAR struct tcp_conn_s *conn;
  FAR struct devif_callback_s *cb;

  DEBUGASSERT(psock != NULL && psock->s_conn != NULL);
  conn = (FAR struct tcp_conn_s *)psock->s_conn;

  /* Find and free the the connection event callback */

  net_lock();
  for (cb = conn->connevents;
       cb != NULL && cb->priv != (FAR void *)psock;
       cb = cb->nxtconn)
    {
    }

  if (cb != NULL)
    {
      devif_conn_callback_free(conn->dev, cb, &conn->connevents);
    }

  /* Make sure that this socket is explicitly marked as closed */

  tcp_close_connection(psock, TCP_CLOSE);

  /* Now notify any sockets waiting for events from this particular sockets.
   * Other dup'ed sockets sharing the same connection must not be effected.
   */

  /* REVISIT:  The following logic won't work:  There is no way to compare
   * psocks to check for a match.  This missing logic could only be an issue
   * if the same socket were being used on one thread, but then closed on
   * another.  Some redesign would be required to find only those event
   * handlers that are waiting specifically for this socket (vs. a dup of
   * this socket)
   */

#if 0
  for (cb = conn->list; cb != NULL; cb = cb->nxtconn)
    {
      if (cb->event != NULL && (cb->flags & TCP_CLOSE) != 0)
        {
          cb->event(conn->dev, conn, cb->priv, TCP_CLOSE);
        }
    }
#endif

  net_unlock();
}

/****************************************************************************
 * Name: tcp_lost_connection
 *
 * Description:
 *   Called when a loss-of-connection event has been detected by network
 *   event handling logic.  Perform operations like tcp_stop_monitor but (1)
 *   explicitly mark this socket and (2) disable further callbacks the to the
 *   event handler.
 *
 * Input Parameters:
 *   psock - The TCP socket structure whose connection was lost.
 *   cb    - devif callback structure
 *   flags - Set of connection events events
 *
 * Returned Value:
 *   None
 *
 * Assumptions:
 *   The caller holds the network lock (if not, it will be locked momentarily
 *   by this function).
 *
 ****************************************************************************/

void tcp_lost_connection(FAR struct socket *psock,
                         FAR struct devif_callback_s *cb, uint16_t flags)
{
  DEBUGASSERT(psock != NULL && psock->s_conn != NULL);

  /* Nullify the callback structure so that recursive callbacks are not
   * received by the event handler due to disconnection processing.
   *
   * NOTE: In a configuration with CONFIG_NET_TCP_WRITE_BUFFERS=y,
   * the "semi-permanent" callback structure may have already been
   * nullified.
   */

  if (cb != NULL)
    {
      cb->flags = 0;
      cb->priv  = NULL;
      cb->event = NULL;
    }

  /* Make sure that this socket is explicitly marked.  It may not get a
   * callback due to the above nullification.
   */

  tcp_close_connection(psock, flags);

  /* Then stop the network monitor for all sockets. */

  tcp_shutdown_monitor((FAR struct tcp_conn_s *)psock->s_conn, flags);
}

#endif /* NET_TCP_HAVE_STACK */
