#ifndef IDGEN_TUNNEL_H
#define IDGEN_TUNNEL_H

#include <muduo/base/Logging.h>
#include <muduo/net/EventLoop.h>
#include <muduo/net/InetAddress.h>
#include <muduo/net/TcpClient.h>
#include <muduo/net/TcpServer.h>

#include <boost/bind.hpp>
#include <boost/enable_shared_from_this.hpp>

#include <glog/logging.h>

namespace idgen {
class Tunnel : public boost::enable_shared_from_this<Tunnel>,
               boost::noncopyable
{
 public:
  Tunnel(muduo::net::EventLoop* loop,
         const muduo::net::InetAddress& serverAddr,
         const muduo::net::TcpConnectionPtr& serverConn)
    : client_(loop, serverAddr, serverConn->name()),
      serverConn_(serverConn)
  {
    VLOG(1) << "Tunnel " << serverConn->peerAddress().toIpPort()
             << " <-> " << serverAddr.toIpPort();
  }

  ~Tunnel()
  {
    VLOG(1) << "~Tunnel";
  }

  void setup()
  {
    client_.setConnectionCallback(
        boost::bind(&Tunnel::onClientConnection, shared_from_this(), _1));
    client_.setMessageCallback(
        boost::bind(&Tunnel::onClientMessage, shared_from_this(), _1, _2, _3));
    serverConn_->setHighWaterMarkCallback(
        boost::bind(&Tunnel::onHighWaterMarkWeak, boost::weak_ptr<Tunnel>(shared_from_this()), _1, _2),
        10*1024*1024);
  }

  void teardown()
  {
    client_.setConnectionCallback(muduo::net::defaultConnectionCallback);
    client_.setMessageCallback(muduo::net::defaultMessageCallback);
    CHECK(serverConn_);
    serverConn_->shutdown();
  }

  void connect()
  {
    client_.connect();
  }

  void disconnect()
  {
    client_.disconnect();
    // serverConn_.reset();
  }

  muduo::net::TcpConnectionPtr remoteConn() const
  {
    return client_.connection();
  }

  void onClientConnection(const muduo::net::TcpConnectionPtr& conn)
  {
    if (conn->connected()) {
      // 在tunnel建立之前serverConn_可能已经关闭了连接
      if (!serverConn_->connected()) {
        VLOG(1) << "server connection has been shutdown before tunnel connected.";
        conn->shutdown();
        return;
      }
      conn->setTcpNoDelay(true);
      conn->setHighWaterMarkCallback(
          boost::bind(&Tunnel::onHighWaterMarkWeak, boost::weak_ptr<Tunnel>(shared_from_this()), _1, _2),
          10*1024*1024);
      if (serverConn_->inputBuffer()->readableBytes() > 0) {
        conn->send(serverConn_->inputBuffer());
      }
    } else {
      teardown();
    }
  }

  void onClientMessage(const muduo::net::TcpConnectionPtr& conn,
                       muduo::net::Buffer* buf,
                       muduo::Timestamp)
  {
    if (serverConn_) {
      serverConn_->send(buf);
    } else {
      buf->retrieveAll();
      abort();
    }
  }

  void onHighWaterMark(const muduo::net::TcpConnectionPtr& conn,
                       size_t bytesToSent)
  {
    LOG_INFO << "onHighWaterMark " << conn->name()
             << " bytes " << bytesToSent;
    disconnect();
  }

  static void onHighWaterMarkWeak(const boost::weak_ptr<Tunnel>& wkTunnel,
                                  const muduo::net::TcpConnectionPtr& conn,
                                  size_t bytesToSent)
  {
    boost::shared_ptr<Tunnel> tunnel = wkTunnel.lock();
    if (tunnel) {
      tunnel->onHighWaterMark(conn, bytesToSent);
    }
  }

 private:
  muduo::net::TcpClient client_;
  muduo::net::TcpConnectionPtr serverConn_;
};
}

#endif
