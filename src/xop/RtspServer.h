// PHZ
// 2020-4-2

#ifndef XOP_RTSP_SERVER_H
#define XOP_RTSP_SERVER_H

#include <memory>
#include <string>
#include <mutex>
#include <unordered_map>
#include "net/TcpServer.h"
#include "rtsp.h"

namespace xop
{
class RtspConnection;

class RtspServer : public Rtsp, public TcpServer
{
public:
	enum ServerType
	{
		FILE_SERVER,
		PUSH_SERVER
	};
public:    
	static std::shared_ptr<RtspServer> Create(xop::EventLoop* loop);
	~RtspServer();

	void SetServerType(ServerType type) { _type = type; }
    MediaSessionId AddSession(MediaSession* session);
    void RemoveSession(MediaSessionId sessionId);

    bool PushFrame(MediaSessionId sessionId, MediaChannelId channelId, AVFrame& frame);

private:
    friend class RtspConnection;

	RtspServer(xop::EventLoop* loop);
    MediaSessionPtr LookMediaSession(const std::string& suffix);
    MediaSessionPtr LookMediaSession(MediaSessionId sessionId);
    virtual TcpConnection::Ptr OnConnect(SOCKET sockfd);

	ServerType _type = PUSH_SERVER;
    std::mutex mutex_;
    std::unordered_map<MediaSessionId, std::shared_ptr<MediaSession>> media_sessions_;
    std::unordered_map<std::string, MediaSessionId> rtsp_suffix_map_;
};

}

#endif

