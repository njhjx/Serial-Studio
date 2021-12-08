/*
 * Copyright (c) 2020-2021 Alex Spataru <https://github.com/alex-spataru>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "Network.h"

#include <IO/Manager.h>
#include <Misc/Utilities.h>

namespace IO
{
namespace DataSources
{
static Network *NETWORK = Q_NULLPTR;

/**
 * Constructor function
 */
Network::Network()
    : m_hostExists(false)
    , m_udpMulticast(false)
    , m_lookupActive(false)
{
    setRemoteAddress("");
    setTcpPort(defaultTcpPort());
    setUdpLocalPort(defaultUdpLocalPort());
    setUdpRemotePort(defaultUdpRemotePort());
    setSocketType(QAbstractSocket::TcpSocket);
    connect(&m_tcpSocket, &QTcpSocket::errorOccurred, this, &Network::onErrorOccurred);
    connect(&m_udpSocket, &QUdpSocket::errorOccurred, this, &Network::onErrorOccurred);
}

/**
 * Destructor function
 */
Network::~Network()
{
    disconnectDevice();
}

/**
 * Returns the only instance of this class
 */
Network *Network::getInstance()
{
    if (!NETWORK)
        NETWORK = new Network;

    return NETWORK;
}

/**
 * Returns the host address
 */
QString Network::remoteAddress() const
{
    return m_address;
}

/**
 * Returns the TCP port number
 */
quint16 Network::tcpPort() const
{
    return m_tcpPort;
}

/**
 * Returns the UDP local port number
 */
quint16 Network::udpLocalPort() const
{
    return m_udpLocalPort;
}

/**
 * Returns the UDP remote port number
 */
quint16 Network::udpRemotePort() const
{
    return m_udpRemotePort;
}

/**
 * Returns @c true if the UDP socket is managing a multicasted
 * connection.
 */
bool Network::udpMulticast() const
{
    return m_udpMulticast;
}

/**
 * Returns @c true if we are currently performing a DNS lookup
 */
bool Network::lookupActive() const
{
    return m_lookupActive;
}

/**
 * Returns the current socket type as an index of the list returned by the @c socketType
 * function.
 */
int Network::socketTypeIndex() const
{
    switch (socketType())
    {
        case QAbstractSocket::TcpSocket:
            return 0;
            break;
        case QAbstractSocket::UdpSocket:
            return 1;
            break;
        default:
            return -1;
            break;
    }
}

/**
 * Returns @c true if the port is greater than 0 and the host address is valid.
 */
bool Network::configurationOk() const
{
    return tcpPort() > 0 && m_hostExists;
}

/**
 * Returns a list with the available socket types
 */
StringList Network::socketTypes() const
{
    return StringList { "TCP", "UDP" };
}

/**
 * Returns the socket type. Valid return values are:
 *
 * @c QAbstractSocket::TcpSocket
 * @c QAbstractSocket::UdpSocket
 * @c QAbstractSocket::SctpSocket
 * @c QAbstractSocket::UnknownSocketType
 */
QAbstractSocket::SocketType Network::socketType() const
{
    return m_socketType;
}

/**
 * Attempts to make a connection to the given host, port and TCP/UDP socket type.
 */
QIODevice *Network::openNetworkPort()
{
    // Disconnect all sockets
    disconnectDevice();

    // Init socket pointer
    QAbstractSocket *socket = Q_NULLPTR;

    // Get host & port
    auto hostAddr = remoteAddress();
    if (hostAddr.isEmpty())
        hostAddr = defaultAddress();

    // TCP connection, assign socket pointer & connect to host
    if (socketType() == QAbstractSocket::TcpSocket)
    {
        socket = &m_tcpSocket;
        m_tcpSocket.connectToHost(hostAddr, tcpPort());
    }

    // UDP connection, assign socket pointer & bind to host
    else if (socketType() == QAbstractSocket::UdpSocket)
    {
        // Bind the UDP socket
        // clang-format off
        m_udpSocket.bind(udpLocalPort(),
                         QAbstractSocket::ShareAddress |
                         QAbstractSocket::ReuseAddressHint);
        // clang-format on

        // Join the multicast group (if required)
        if (udpMulticast())
            m_udpSocket.joinMulticastGroup(QHostAddress(m_address));

        // Update socket pointer
        socket = &m_udpSocket;
    }

    // Return pointer
    return socket;
}

/**
 * Instructs the module to communicate via a TCP socket.
 */
void Network::setTcpSocket()
{
    setSocketType(QAbstractSocket::TcpSocket);
}

/**
 * Instructs the module to communicate via an UDP socket.
 */
void Network::setUdpSocket()
{
    setSocketType(QAbstractSocket::UdpSocket);
}

/**
 * Disconnects the TCP/UDP sockets from the host
 */
void Network::disconnectDevice()
{
    m_tcpSocket.abort();
    m_udpSocket.abort();
    m_tcpSocket.disconnectFromHost();
    m_udpSocket.disconnectFromHost();
}

/**
 * Changes the TCP socket's @c port number
 */
void Network::setTcpPort(const quint16 port)
{
    m_tcpPort = port;
    Q_EMIT portChanged();
}

/**
 * Changes the UDP socket's local @c port number
 */
void Network::setUdpLocalPort(const quint16 port)
{
    m_udpLocalPort = port;
    Q_EMIT portChanged();
}

/**
 * Changes the UDP socket's remote @c port number
 */
void Network::setUdpRemotePort(const quint16 port)
{
    m_udpRemotePort = port;
    Q_EMIT portChanged();
}

/**
 * Sets the IPv4 or IPv6 address specified by the input string representation
 */
void Network::setRemoteAddress(const QString &address)
{
    // Check if host name exists
    if (QHostAddress(address).isNull())
    {
        m_hostExists = false;
        lookup(address);
    }

    // Host is an IP address, host should exist
    else
        m_hostExists = true;

    // Change host
    m_address = address;
    Q_EMIT addressChanged();
}

/**
 * Performs a DNS lookup for the given @a host name
 */
void Network::lookup(const QString &host)
{
    m_lookupActive = true;
    Q_EMIT lookupActiveChanged();
    QHostInfo::lookupHost(host.simplified(), this, &Network::lookupFinished);
}

/**
 * Enables/Disables multicast connections with the UDP socket.
 */
void Network::setUdpMulticast(const bool enabled)
{
    m_udpMulticast = enabled;
    Q_EMIT udpMulticastChanged();
}

/**
 * Changes the current socket type given an index of the list returned by the
 * @c socketType() function.
 */
void Network::setSocketTypeIndex(const int index)
{
    switch (index)
    {
        case 0:
            setTcpSocket();
            break;
        case 1:
            setUdpSocket();
            break;
        default:
            break;
    }
}

/**
 * Changes the socket type. Valid input values are:
 *
 * @c QAbstractSocket::TcpSocket
 * @c QAbstractSocket::UdpSocket
 * @c QAbstractSocket::UnknownSocketType
 */
void Network::setSocketType(const QAbstractSocket::SocketType type)
{
    m_socketType = type;
    Q_EMIT socketTypeChanged();
}

/**
 * Sets the host IP address when the lookup finishes.
 * If the lookup fails, the error code/string shall be shown to the user in a messagebox.
 */
void Network::lookupFinished(const QHostInfo &info)
{
    m_lookupActive = false;
    Q_EMIT lookupActiveChanged();

    if (info.error() == QHostInfo::NoError)
    {
        auto addresses = info.addresses();
        if (addresses.count() >= 1)
        {
            m_hostExists = true;
            Q_EMIT addressChanged();
            return;
        }
    }
}

/**
 * This function is called whenever a socket error occurs, it disconnects the socket
 * from the host and displays the error in a message box.
 */
void Network::onErrorOccurred(const QAbstractSocket::SocketError socketError)
{
    QString error;
    if (socketType() == QAbstractSocket::TcpSocket)
        error = m_tcpSocket.errorString();
    else if (socketType() == QAbstractSocket::UdpSocket)
        error = m_udpSocket.errorString();
    else
        error = QString::number(socketError);

    Manager::getInstance()->disconnectDevice();
    Misc::Utilities::showMessageBox(tr("Network socket error"), error);
}
}
}
